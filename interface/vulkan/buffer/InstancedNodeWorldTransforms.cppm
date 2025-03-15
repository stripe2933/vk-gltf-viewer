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

/**
 * Convert the span of \p U to the span of \p T. The result span byte size must be same as the \p span's.
 * @tparam T Result span type.
 * @tparam U Source span type.
 * @param span Source span.
 * @return Converted span.
 * @note Since the source and result span sizes must be same, <tt>span.size_bytes()</tt> must be divisible by <tt>sizeof(T)</tt>.
 */
template <typename T, typename U>
[[nodiscard]] std::span<T> reinterpret_span(std::span<U> span) {
    if (span.size_bytes() % sizeof(T) != 0) {
        throw std::invalid_argument { "Span size mismatch: span of T does not fully fit into the current span." };
    }

    return { reinterpret_cast<T*>(span.data()), span.size_bytes() / sizeof(T) };
}

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * @brief Buffer that stores the mesh nodes' transform matrices, with flattened instance matrices.
     *
     * The term "mesh node" means a node that has a mesh. This buffer only contains transform matrices of mesh nodes. In other words, <tt>buffer.asRange<const fastgltf::math::fmat4x4>()[nodeIndex]</tt> may NOT represent the world transformation matrix of the <tt>nodeIndex</tt>-th node, because maybe there were nodes with no mesh prior to the <tt>nodeIndex</tt>-th node.
     *
     * For example, a scene has 4 nodes (denoted as A B C D) and A has 2 instances (<tt>M1</tt>, <tt>M2</tt>), B has 3 instances (<tt>M3</tt>, <tt>M4</tt>, <tt>M5</tt>), C is meshless, and D has 1 instance (<tt>M6</tt>), then the flattened matrices will be laid out as:
     * @code
     * [MA * M1, MA * M2, MB * M3, MB * M4, MB * M5, MD * M6]
     * @endcode
     * Be careful that there is no transform matrix related about node C, because it is meshless.
     */
    export class InstancedNodeWorldTransforms {
    public:
        /**
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param asset glTF asset.
         * @param nodeInstanceCountExclusiveScanWithCount pre-calculated scanned instance counts, with additional total count at the end.
         * @param nodeWorldTransforms pre-calculated node world transforms.
         * @param allocator VMA allocator.
         * @param adapter Buffer data adapter.
         * @note This will fill the buffer data with each node's local transform (and post-multiplied instance transforms if presented), as the scene structure is not provided. You have to call <tt>update</tt> to update the world transforms.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        InstancedNodeWorldTransforms(
            const fastgltf::Asset &asset LIFETIMEBOUND,
            std::shared_ptr<const gltf::ds::NodeInstanceCountExclusiveScanWithCount> nodeInstanceCountExclusiveScanWithCount,
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
            vma::Allocator allocator,
            const BufferDataAdapter &adapter = {}
        ) : asset { asset },
            nodeInstanceCountExclusiveScanWithCount { std::move(nodeInstanceCountExclusiveScanWithCount) },
            buffer { allocator, vk::BufferCreateInfo {
                {},
                sizeof(fastgltf::math::fmat4x4) * this->nodeInstanceCountExclusiveScanWithCount->back(),
                vk::BufferUsageFlagBits::eStorageBuffer,
            } },
            descriptorInfo { buffer, 0, vk::WholeSize } {
            const std::span data = buffer.asRange<fastgltf::math::fmat4x4>();
            for (const auto &[nodeIndex, node] : asset.nodes | ranges::views::enumerate) {
                if (!node.meshIndex) {
                    continue;
                }

                if (node.instancingAttributes.empty()) {
                    data[(*this->nodeInstanceCountExclusiveScanWithCount)[nodeIndex]] = nodeWorldTransforms[nodeIndex];
                }
                else {
                    std::ranges::transform(
                        getInstanceTransforms(asset, nodeIndex, adapter), &data[(*this->nodeInstanceCountExclusiveScanWithCount)[nodeIndex]],
                        [&](const fastgltf::math::fmat4x4 &instanceTransform) {
                            return nodeWorldTransforms[nodeIndex] * instanceTransform;
                        });
                }
            }
        }

        [[nodiscard]] std::uint32_t getStartIndex(std::size_t nodeIndex) const noexcept {
            return (*nodeInstanceCountExclusiveScanWithCount)[nodeIndex];
        }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        /**
         * @brief Update the mesh node world transforms from given \p nodeIndex, to its descendants.
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
                if (!node.meshIndex) {
                    return;
                }

                if (node.instancingAttributes.empty()) {
                    bufferData[(*nodeInstanceCountExclusiveScanWithCount)[nodeIndex]] = nodeWorldTransforms[nodeIndex];
                }
                else {
                    std::ranges::transform(
                        getInstanceTransforms(asset, nodeIndex, adapter),
                        &bufferData[(*nodeInstanceCountExclusiveScanWithCount)[nodeIndex]],
                        [&](const fastgltf::math::fmat4x4 &instanceTransform) {
                            return nodeWorldTransforms[nodeIndex] * instanceTransform;
                        });
                }
            });
        }

        /**
         * @brief Update the mesh node world transforms for all nodes in a scene.
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
        std::shared_ptr<const gltf::ds::NodeInstanceCountExclusiveScanWithCount> nodeInstanceCountExclusiveScanWithCount;
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
    };
}