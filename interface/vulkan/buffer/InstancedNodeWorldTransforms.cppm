module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.buffer.InstancedNodeWorldTransforms;

import std;
import vku;
export import vk_mem_alloc_hpp;
export import vulkan_hpp;
import :gltf.algorithm.traversal;
export import :gltf.data_structure.NodeInstanceCountExclusiveScanWithCount;
export import :gltf.NodeWorldTransforms;
import :helpers.algorithm;
import :helpers.fastgltf;
import :helpers.ranges;
import :helpers.span;

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * @brief Buffer that stores the nodes' transform matrices, with flattened instance matrices.
     */
    export class InstancedNodeWorldTransforms {
    public:
        /**
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param asset glTF asset.
         * @param scene Scene represents the node hierarchy.
         * @param nodeInstanceCountExclusiveScanWithCount pre-calculated scanned instance counts, with additional total count at the end.
         * @param nodeWorldTransforms pre-calculated node world transforms.
         * @param allocator VMA allocator.
         * @param adapter Buffer data adapter.
         * @note This will fill the buffer data with each node's local transform (and post-multiplied instance transforms if presented), as the scene structure is not provided. You have to call <tt>update</tt> to update the world transforms.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        InstancedNodeWorldTransforms(
            const fastgltf::Asset &asset LIFETIMEBOUND,
            const fastgltf::Scene &scene,
            const gltf::ds::NodeInstanceCountExclusiveScanWithCount &nodeInstanceCountExclusiveScanWithCount LIFETIMEBOUND,
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
            vma::Allocator allocator,
            const BufferDataAdapter &adapter = {}
        ) : asset { asset },
            nodeInstanceCountExclusiveScanWithCount { nodeInstanceCountExclusiveScanWithCount },
            buffer { allocator, vk::BufferCreateInfo {
                {},
                sizeof(fastgltf::math::fmat4x4) * nodeInstanceCountExclusiveScanWithCount.back(),
                vk::BufferUsageFlagBits::eStorageBuffer,
            } },
            descriptorInfo { buffer, 0, vk::WholeSize } {
            update(scene, nodeWorldTransforms, adapter);
        }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        /**
         * @brief Update the node world transforms from given \p nodeIndex, to its descendants.
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param nodeIndex Node index to be started.
         * @param nodeWorldTransforms pre-calculated node world transforms.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void update(
            std::size_t nodeIndex,
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            const std::span bufferData = buffer.asRange<fastgltf::math::fmat4x4>();
            gltf::algorithm::traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
                const fastgltf::Node &node = asset.get().nodes[nodeIndex];
                if (node.instancingAttributes.empty()) {
                    bufferData[nodeInstanceCountExclusiveScanWithCount.get()[nodeIndex]] = nodeWorldTransforms[nodeIndex];
                }
                else {
                    std::ranges::transform(
                        getInstanceTransforms(asset, nodeIndex, adapter),
                        &bufferData[nodeInstanceCountExclusiveScanWithCount.get()[nodeIndex]],
                        [&](const fastgltf::math::fmat4x4 &instanceTransform) {
                            return nodeWorldTransforms[nodeIndex] * instanceTransform;
                        });
                }
            });
        }

        /**
         * @brief Update the node world transforms for all nodes in a scene.
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param scene Scene to be updated.
         * @param nodeWorldTransforms pre-calculated node world transforms.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void update(
            const fastgltf::Scene &scene,
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            for (std::size_t nodeIndex : scene.nodeIndices) {
                update(nodeIndex, nodeWorldTransforms, adapter);
            }
        }

    private:
        std::reference_wrapper<const fastgltf::Asset> asset;
        std::reference_wrapper<const gltf::ds::NodeInstanceCountExclusiveScanWithCount> nodeInstanceCountExclusiveScanWithCount;
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
    };
}