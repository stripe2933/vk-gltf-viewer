export module vk_gltf_viewer:vulkan.buffer.SceneInstancedNodeWorldTransforms;

import std;
import :gltf.algorithm.traversal;
export import :gltf.SceneNodeWorldTransforms;
import :helpers.ranges;
export import :vulkan.Gpu;

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
    export class SceneInstancedNodeWorldTransforms {
    public:
        /**
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param asset glTF asset.
         * @param scene Scene to be used for world transform calculation
         * @param sceneNodeWorldTransforms pre-calculated scene node world transforms.
         * @param gpu Vulkan GPU.
         * @param adapter Buffer data adapter.
         * @note This will fill the buffer data with each node's local transform (and post-multiplied instance transforms if presented), as the scene structure is not provided. If the root nodes have to be specified, use the overloaded constructor.
         * @note You have to call <tt>update</tt> to update the world transforms.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        SceneInstancedNodeWorldTransforms(
            const fastgltf::Asset &asset [[clang::lifetimebound]],
            const fastgltf::Scene &scene,
            const gltf::SceneNodeWorldTransforms &sceneNodeWorldTransforms,
            const Gpu &gpu [[clang::lifetimebound]],
            const BufferDataAdapter &adapter = {}
        ) : asset { asset },
            instanceOffsets { createInstanceOffsets() },
            buffer { createBuffer(gpu.allocator, adapter) },
            bufferAddress { gpu.device.getBufferAddress({ buffer }) } {
            update(scene, sceneNodeWorldTransforms, adapter);
        }

        [[nodiscard]] vk::DeviceAddress getTransformStartAddress(std::size_t nodeIndex) const noexcept {
            return bufferAddress + sizeof(fastgltf::math::fmat4x4) * instanceOffsets[nodeIndex];
        }

        /**
         * @brief Get world transform matrix of \p nodeIndex-th mesh node's \p instanceIndex-th instance in the scene.
         *
         * The term "mesh node" means a node that has a mesh, and this function only cares about mesh nodes (you MUST NOT pass a node index that doesn't have a mesh).
         *
         * @param nodeIndex Index of the mesh node.
         * @param instanceIndex Index of the instance in the node. If EXT_mesh_gpu_instancing extension is not used, this value must be 0 (omitted).
         * @return World transformation matrix of the mesh node's instance, calculated by post-multiply accumulated transformation matrices from scene root.
         * @warning \p nodeIndex-th node MUST have a mesh. No exception thrown for constraint violation.
         * @warning \p instanceIndex-th instance MUST be less than the instance count of the node. No exception thrown for constraint violation.
         */
        const fastgltf::math::fmat4x4 &getTransform(std::size_t nodeIndex, std::uint32_t instanceIndex = 0) const noexcept {
            const std::span meshNodeWorldTransforms = buffer.asRange<const fastgltf::math::fmat4x4>();
            return meshNodeWorldTransforms[instanceOffsets[nodeIndex] + instanceIndex];
        }

        /**
         * @copydoc SceneInstancedNodeWorldTransforms::getTransform(std::size_t,std::uint32_t)
         */
        fastgltf::math::fmat4x4 &getTransform(std::size_t nodeIndex, std::uint32_t instanceIndex = 0) noexcept {
            const std::span meshNodeWorldTransforms = buffer.asRange<fastgltf::math::fmat4x4>();
            return meshNodeWorldTransforms[instanceOffsets[nodeIndex] + instanceIndex];
        }

        /**
         * @brief Update the mesh node world transforms from given \p nodeIndex, to its descendants.
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param nodeIndex Node index to be started.
         * @param sceneNodeWorldTransforms pre-calculated scene node world transforms.
         * @param adapter Buffer data adapter.
         * @pre Node with given \p nodeIndex must be in the scene of \p sceneNodeWorldTransforms.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void update(
            std::size_t nodeIndex,
            const gltf::SceneNodeWorldTransforms &sceneNodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            const std::span bufferData = buffer.asRange<fastgltf::math::fmat4x4>();
            gltf::algorithm::traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
                const fastgltf::Node &node = asset.get().nodes[nodeIndex];
                if (!node.meshIndex) {
                    return;
                }

                if (std::vector instanceTransforms = getInstanceTransforms(asset, node, adapter); !instanceTransforms.empty()) {
                    std::ranges::transform(
                        instanceTransforms, &bufferData[instanceOffsets[nodeIndex]],
                        [&](const fastgltf::math::fmat4x4 &instanceTransform) {
                            return sceneNodeWorldTransforms.worldTransforms[nodeIndex] * instanceTransform;
                        });
                }
                else {
                    bufferData[instanceOffsets[nodeIndex]] = sceneNodeWorldTransforms.worldTransforms[nodeIndex];
                }
            });
        }

        /**
         * @brief Update the mesh node world transforms for all nodes in a scene.
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
         * @param scene Scene to be updated.
         * @param sceneNodeWorldTransforms pre-calculated scene node world transforms.
         * @param adapter Buffer data adapter.
         * @pre Given \p scene must be same scene of \p sceneNodeWorldTransforms construction.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void update(
            const fastgltf::Scene &scene,
            const gltf::SceneNodeWorldTransforms &sceneNodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            for (std::size_t nodeIndex : scene.nodeIndices) {
                update(nodeIndex, sceneNodeWorldTransforms, adapter);
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
        vk::DeviceAddress bufferAddress;

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
        [[nodiscard]] vku::MappedBuffer createBuffer(vma::Allocator allocator, const BufferDataAdapter &adapter) const {
            vku::MappedBuffer result { allocator, vk::BufferCreateInfo {
                {},
                sizeof(fastgltf::math::fmat4x4) * instanceOffsets.back(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            }, vku::allocation::hostRead };

            const std::span data = result.asRange<fastgltf::math::fmat4x4>();
            for (std::size_t nodeIndex : ranges::views::upto(asset.get().nodes.size())) {
                const fastgltf::Node &node = asset.get().nodes[nodeIndex];
                const fastgltf::math::fmat4x4 nodeTransform = visit(fastgltf::visitor {
                    [](const fastgltf::TRS &trs) { return toMatrix(trs); },
                    [](fastgltf::math::fmat4x4 matrix) { return matrix; },
                }, node.transform);

                if (std::vector instanceTransforms = getInstanceTransforms(asset, node, adapter); !instanceTransforms.empty()) {
                    std::ranges::transform(
                        instanceTransforms, &data[instanceOffsets[nodeIndex]],
                        [&](const fastgltf::math::fmat4x4 &instanceTransform) {
                            return nodeTransform * instanceTransform;
                        });
                }
                else {
                    data[instanceOffsets[nodeIndex]] = nodeTransform;
                }
            }

            return result;
        }
    };
}