module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.Nodes;

import std;
export import fastgltf;
import vku;
export import vk_mem_alloc_hpp;
import :helpers.fastgltf;
import :helpers.ranges;
import :gltf.algorithm.traversal;
export import :gltf.data_structure.NodeInstanceCountExclusiveScanWithCount;
export import :gltf.data_structure.TargetWeightCountExclusiveScanWithCount;
export import :gltf.data_structure.SkinJointCountExclusiveScanWithCount;
export import :gltf.NodeWorldTransforms;
import :vulkan.shader_type.Node;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Nodes {
    public:
        Nodes(
            const fastgltf::Asset &asset,
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
            const gltf::ds::NodeInstanceCountExclusiveScanWithCount &nodeInstanceCountExclusiveScan,
            const gltf::ds::TargetWeightCountExclusiveScanWithCount &targetWeightCountExclusiveScan,
            const gltf::ds::SkinJointCountExclusiveScanWithCount &skinJointCountExclusiveScan,
            vma::Allocator allocator
        ) : asset { asset },
            buffer {
                allocator,
                std::from_range, ranges::views::upto(asset.nodes.size()) | std::views::transform([&](std::size_t nodeIndex) {
                    const fastgltf::Node &node = asset.nodes[nodeIndex];
                    return shader_type::Node {
                        .worldTransform = glm::make_mat4(nodeWorldTransforms[nodeIndex].data()),
                        .instancedTransformStartIndex = nodeInstanceCountExclusiveScan[nodeIndex],
                        .morphTargetWeightStartIndex = targetWeightCountExclusiveScan[nodeIndex],
                        .skinJointIndexStartIndex = to_optional(node.skinIndex).transform([&](std::size_t skinIndex) {
                            return skinJointCountExclusiveScan[skinIndex];
                        }).value_or(std::numeric_limits<std::uint32_t>::max()),
                    };
                }),
                vk::BufferUsageFlagBits::eStorageBuffer,
            },
            descriptorInfo { buffer, 0, vk::WholeSize } { }

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
    };
}