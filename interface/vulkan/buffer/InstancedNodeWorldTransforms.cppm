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
import :helpers.algorithm;
import :helpers.fastgltf;
import :helpers.ranges;
import :helpers.span;

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * @brief Buffer that stores the instanced nodes' transform matrices as flattened form.
     *
     * If EXT_mesh_gpu_instancing is unused or an asset doesn't have any instanced node, this class must not be instantiated
     * (as it will cause the zero-sized buffer creation).
     */
    export class InstancedNodeWorldTransforms : public vku::MappedBuffer {
        std::reference_wrapper<const fastgltf::Asset> asset;

    public:
        std::reference_wrapper<const gltf::ds::NodeInstanceCountExclusiveScanWithCount> nodeInstanceCountExclusiveScanWithCount;

        /**
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param allocator VMA allocator.
         * @param asset glTF asset.
         * @param scene Scene represents the node hierarchy.
         * @param nodeInstanceCountExclusiveScanWithCount pre-calculated scanned instance counts, with additional total count at the end.
         * @param nodeWorldTransforms Node world transform matrices ordered by node indices in the asset.
         * @param adapter Buffer data adapter.
         * @note This will fill the buffer data with each node's local transform (and post-multiplied instance transforms if presented), as the scene structure is not provided. You have to call <tt>update</tt> to update the world transforms.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        InstancedNodeWorldTransforms(
            vma::Allocator allocator LIFETIMEBOUND,
            const fastgltf::Asset &asset LIFETIMEBOUND,
            const fastgltf::Scene &scene,
            const gltf::ds::NodeInstanceCountExclusiveScanWithCount &nodeInstanceCountExclusiveScanWithCount LIFETIMEBOUND,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) : MappedBuffer { allocator, vk::BufferCreateInfo {
                {},
                sizeof(fastgltf::math::fmat4x4) * nodeInstanceCountExclusiveScanWithCount.back(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            } },
            asset { asset },
            nodeInstanceCountExclusiveScanWithCount { nodeInstanceCountExclusiveScanWithCount } {
            update(scene, nodeWorldTransforms, adapter);
        }

        /**
         * @brief Get instanced world transform matrices for a node whose index is \p nodeIndex.
         * @param nodeIndex Node index.
         * @return A span of instanced world transform matrices for the node.
         */
        [[nodiscard]] std::span<const fastgltf::math::fmat4x4> getTransforms(std::size_t nodeIndex) const noexcept {
            const std::size_t offset = nodeInstanceCountExclusiveScanWithCount.get()[nodeIndex];
            const std::size_t count = nodeInstanceCountExclusiveScanWithCount.get()[nodeIndex + 1] - offset;
            return asRange<const fastgltf::math::fmat4x4>().subspan(offset, count);
        }

        /**
         * @copydoc getTransforms(std::size_t nodeIndex) const noexcept
         */
        [[nodiscard]] std::span<fastgltf::math::fmat4x4> getTransforms(std::size_t nodeIndex) noexcept {
            const std::size_t offset = nodeInstanceCountExclusiveScanWithCount.get()[nodeIndex];
            const std::size_t count = nodeInstanceCountExclusiveScanWithCount.get()[nodeIndex + 1] - offset;
            return asRange<fastgltf::math::fmat4x4>().subspan(offset, count);
        }

        /**
         * @brief Update the node world transform at \p nodeIndex.
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param nodeIndex Node index.
         * @param nodeWorldTransform World transform matrix of the node.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void update(
            std::size_t nodeIndex,
            const fastgltf::math::fmat4x4 &nodeWorldTransform,
            const BufferDataAdapter &adapter = {}
        ) {
            const fastgltf::Node &node = asset.get().nodes[nodeIndex];
            if (!node.instancingAttributes.empty()) {
                std::ranges::transform(
                    getInstanceTransforms(asset, nodeIndex, adapter),
                    getTransforms(nodeIndex).begin(),
                    [&](const fastgltf::math::fmat4x4 &instanceTransform) {
                        return nodeWorldTransform * instanceTransform;
                    });
            }
        }

        /**
         * @brief Update the node world transforms from given \p nodeIndex, to its descendants.
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param nodeIndex Node index to be started.
         * @param nodeWorldTransforms Node world transform matrices ordered by node indices in the asset.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void updateHierarchical(
            std::size_t nodeIndex,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            gltf::algorithm::traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
                const fastgltf::Node &node = asset.get().nodes[nodeIndex];
                if (!node.instancingAttributes.empty()) {
                    std::ranges::transform(
                        getInstanceTransforms(asset, nodeIndex, adapter),
                        getTransforms(nodeIndex).begin(),
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
         * @param nodeWorldTransforms Node world transform matrices ordered by node indices in the asset.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void update(
            const fastgltf::Scene &scene,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            for (std::size_t nodeIndex : scene.nodeIndices) {
                updateHierarchical(nodeIndex, nodeWorldTransforms, adapter);
            }
        }
    };
}