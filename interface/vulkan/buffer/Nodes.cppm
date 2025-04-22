module;

#include <cstddef>

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.buffer.Nodes;

import std;
export import fastgltf;
import vku;
export import vk_mem_alloc_hpp;
import :helpers.fastgltf;
import :helpers.ranges;
import :gltf.algorithm.traversal;
export import :gltf.data_structure.TargetWeightCountExclusiveScanWithCount;
export import :gltf.data_structure.SkinJointCountExclusiveScanWithCount;
export import :gltf.NodeWorldTransforms;
export import :vulkan.buffer.InstancedNodeWorldTransforms;
import :vulkan.shader_type.Node;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Nodes {
    public:
        Nodes(
            const vk::raii::Device &device LIFETIMEBOUND,
            vma::Allocator allocator LIFETIMEBOUND,
            const fastgltf::Asset &asset LIFETIMEBOUND,
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
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
            const std::span data = buffer.asRange<shader_type::Node>();
            for (const auto &[nodeIndex, node] : asset.nodes | ranges::views::enumerate) {
                data[nodeIndex] = shader_type::Node {
                    .worldTransform = glm::make_mat4(nodeWorldTransforms[nodeIndex].data()),
                    .pInstancedWorldTransforms = [&]() -> vk::DeviceAddress {
                        if (node.instancingAttributes.empty()) {
                            // Use address of self's worldTransform if no instancing attributes are presented.
                            return deviceAddress + sizeof(shader_type::Node) * nodeIndex + offsetof(shader_type::Node, worldTransform);
                        }

                        // Use address of instanced node world transform buffer if instancing attributes are presented.
                        return instancedNodeWorldTransformBuffer->getDeviceAddress()
                            + sizeof(glm::mat4) * instancedNodeWorldTransformBuffer->nodeInstanceCountExclusiveScanWithCount.get()[nodeIndex];
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
         * @brief Update the node world transforms from given \p nodeIndex, to its descendants.
         * @param nodeIndex Node index to be started.
         * @param nodeWorldTransforms pre-calculated node world transforms.
         */
        void update(std::size_t nodeIndex, const gltf::NodeWorldTransforms &nodeWorldTransforms) {
            const std::span bufferData = buffer.asRange<shader_type::Node>();
            gltf::algorithm::traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
                bufferData[nodeIndex].worldTransform = glm::make_mat4(nodeWorldTransforms[nodeIndex].data());
            });
        }

        /**
         * @brief Update the node world transforms for all nodes in a scene.
         * @param scene Scene to be updated.
         * @param nodeWorldTransforms pre-calculated node world transforms.
         */
        void update(const fastgltf::Scene &scene, const gltf::NodeWorldTransforms &nodeWorldTransforms) {
            for (std::size_t nodeIndex : scene.nodeIndices) {
                update(nodeIndex, nodeWorldTransforms);
            }
        }

    private:
        std::reference_wrapper<const fastgltf::Asset> asset;
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
        vk::DeviceAddress deviceAddress;
    };
}