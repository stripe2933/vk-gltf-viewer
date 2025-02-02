module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.InstancedNodeWorldTransforms;

import std;
import vku;
export import vk_mem_alloc_hpp;
export import vulkan_hpp;
import :gltf.algorithm.traversal;
export import :gltf.NodeWorldTransforms;
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
    export class InstancedNodeWorldTransforms {
    public:
        /**
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param asset glTF asset.
         * @param nodeWorldTransforms pre-calculated node world transforms.
         * @param allocator VMA allocator.
         * @param adapter Buffer data adapter.
         * @note This will fill the buffer data with each node's local transform (and post-multiplied instance transforms if presented), as the scene structure is not provided. You have to call <tt>update</tt> to update the world transforms.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        InstancedNodeWorldTransforms(
            const fastgltf::Asset &asset [[clang::lifetimebound]],
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
            vma::Allocator allocator,
            const BufferDataAdapter &adapter = {}
        ) : asset { asset },
            instanceOffsets { createInstanceOffsets() },
            buffer { createBuffer(allocator, nodeWorldTransforms, adapter) },
            descriptorInfo { buffer, 0, vk::WholeSize } { }

        [[nodiscard]] std::uint32_t getStartIndex(std::size_t nodeIndex) const noexcept {
            return instanceOffsets[nodeIndex];
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
                    bufferData[instanceOffsets[nodeIndex]] = nodeWorldTransforms[nodeIndex];
                }
                else {
                    std::ranges::transform(
                        getInstanceTransforms(asset, nodeIndex, adapter),
                        &bufferData[instanceOffsets[nodeIndex]],
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

        /**
         * @brief Exclusive scan of the instance counts (or 1 if the node doesn't have any instance), and additional total instance count at the end.
         */
        std::vector<std::uint32_t> instanceOffsets;

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
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;

        [[nodiscard]] std::vector<std::uint32_t> createInstanceOffsets() const {
            std::vector<std::uint32_t> result;
            result.reserve(asset.get().nodes.size() + 1);
            result.append_range(asset.get().nodes | std::views::transform([this](const fastgltf::Node &node) -> std::uint32_t {
                if (!node.meshIndex) {
                    return 0;
                }
                if (node.instancingAttributes.empty()) {
                    return 1;
                }
                else {
                    // According to the EXT_mesh_gpu_instancing specification, all attribute accessors in a given node
                    // must have the same count. Therefore, we can use the count of the first attribute accessor.
                    return asset.get().accessors[node.instancingAttributes[0].accessorIndex].count;
                }

            }));

            // Additional zero at the end will make the last element of the exclusive scan to be the total instance count.
            result.push_back(0);

            std::exclusive_scan(result.cbegin(), result.cend(), result.begin(), 0U);

            return result;
        }

        template <typename BufferDataAdapter>
        [[nodiscard]] vku::MappedBuffer createBuffer(
            vma::Allocator allocator,
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
            const BufferDataAdapter &adapter
        ) const {
            vku::MappedBuffer result { allocator, vk::BufferCreateInfo {
                {},
                sizeof(fastgltf::math::fmat4x4) * instanceOffsets.back(),
                vk::BufferUsageFlagBits::eStorageBuffer,
            } };

            const std::span data = result.asRange<fastgltf::math::fmat4x4>();
            for (const auto &[nodeIndex, node] : asset.get().nodes | ranges::views::enumerate) {
                if (!node.meshIndex) {
                    continue;
                }
                
                if (node.instancingAttributes.empty()) {
                    data[instanceOffsets[nodeIndex]] = nodeWorldTransforms[nodeIndex];
                }
                else {
                    std::ranges::transform(
                        getInstanceTransforms(asset, nodeIndex, adapter), &data[instanceOffsets[nodeIndex]],
                        [&](const fastgltf::math::fmat4x4 &instanceTransform) {
                            return nodeWorldTransforms[nodeIndex] * instanceTransform;
                        });
                }
            }

            return result;
        }
    };
}