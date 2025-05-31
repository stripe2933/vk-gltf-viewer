module;

#include <cstddef>

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.buffer.Nodes;

import std;
export import fastgltf;
import vku;
export import vk_mem_alloc_hpp;

import vk_gltf_viewer.gltf.algorithm.traversal;
export import vk_gltf_viewer.gltf.data_structure.SkinJointCountExclusiveScanWithCount;
export import vk_gltf_viewer.gltf.data_structure.TargetWeightCountExclusiveScanWithCount;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.optional;
import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.vulkan.buffer.InstancedNodeWorldTransforms;
import vk_gltf_viewer.vulkan.shader_type.Node;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Nodes {
    public:
        Nodes(
            const vk::raii::Device &device LIFETIMEBOUND,
            vma::Allocator allocator LIFETIMEBOUND,
            const fastgltf::Asset &asset LIFETIMEBOUND,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const gltf::ds::TargetWeightCountExclusiveScanWithCount &targetWeightCountExclusiveScan,
            const gltf::ds::SkinJointCountExclusiveScanWithCount &skinJointCountExclusiveScan,
            const InstancedNodeWorldTransforms *instancedNodeWorldTransformBuffer LIFETIMEBOUND = nullptr
        ) : asset { asset },
            buffer { allocator, vk::BufferCreateInfo {
                {},
                sizeof(shader_type::Node) * asset.nodes.size(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            } },
            descriptorInfo { buffer, 0, vk::WholeSize },
            deviceAddress { device.getBufferAddress({ buffer.buffer }) }{
            const std::optional<vk::DeviceAddress> instancedNodeWorldTransformBufferAddress = value_if(
                instancedNodeWorldTransformBuffer,
                [&]() { return device.getBufferAddress({ *instancedNodeWorldTransformBuffer }); });

            const std::span data = buffer.asRange<shader_type::Node>();
            for (const auto &[nodeIndex, node] : asset.nodes | ranges::views::enumerate) {
                data[nodeIndex] = shader_type::Node {
                    .worldTransform = glm::make_mat4(nodeWorldTransforms[nodeIndex].data()),
                    .pInstancedWorldTransforms = [&]() -> vk::DeviceAddress {
                        if (node.instancingAttributes.empty()) {
                            // Use address of self's worldTransform if no instancing attributes are presented.
                            return deviceAddress + sizeof(shader_type::Node) * nodeIndex + offsetof(shader_type::Node, worldTransform);
                        }
                        else {
                            // Use address of instanced node world transform buffer if instancing attributes are presented.
                            return *instancedNodeWorldTransformBufferAddress
                                + sizeof(glm::mat4) * instancedNodeWorldTransformBuffer->nodeInstanceCountExclusiveScanWithCount.get()[nodeIndex];
                        }
                    }(),
                    .morphTargetWeightStartIndex = targetWeightCountExclusiveScan[nodeIndex],
                    .skinJointIndexStartIndex = [&]() {
                        if (node.skinIndex) {
                            return skinJointCountExclusiveScan[*node.skinIndex];
                        }
                        return std::numeric_limits<std::uint32_t>::max();
                    }(),
                };
            }
        }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        /**
         * @brief Update the node world transforms at \p nodeIndex.
         * @param nodeIndex Node index to be started.
         * @param nodeWorldTransform World transform matrix of the node.
         */
        void update(std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
            shader_type::Node &node = buffer.asRange<shader_type::Node>()[nodeIndex];
            node.worldTransform = glm::make_mat4(nodeWorldTransform.data());
        }

        /**
         * @brief Update the node world transforms from given \p nodeIndex, to its descendants.
         * @param nodeIndex Node index to be started.
         * @param nodeWorldTransforms Node world transform matrices ordered by node indices in the asset.
         */
        void updateHierarchical(std::size_t nodeIndex, std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms) {
            const std::span bufferData = buffer.asRange<shader_type::Node>();
            gltf::algorithm::traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
                bufferData[nodeIndex].worldTransform = glm::make_mat4(nodeWorldTransforms[nodeIndex].data());
            });
        }

        /**
         * @brief Update the node world transforms for all nodes in a scene.
         * @param scene Scene to be updated.
         * @param nodeWorldTransforms Node world transform matrices that is indexed by node index.
         */
        void update(const fastgltf::Scene &scene, std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms) {
            for (std::size_t nodeIndex : scene.nodeIndices) {
                updateHierarchical(nodeIndex, nodeWorldTransforms);
            }
        }

    private:
        std::reference_wrapper<const fastgltf::Asset> asset;
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
        vk::DeviceAddress deviceAddress;
    };
}