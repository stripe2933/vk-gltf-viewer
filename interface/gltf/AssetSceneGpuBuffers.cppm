export module vk_gltf_viewer:gltf.AssetSceneGpuBuffers;

import std;
export import fastgltf;
import :gltf.algorithm.traversal;
export import :gltf.AssetPrimitiveInfo;
export import :gltf.SceneNodeWorldTransforms;
import :helpers.concepts;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.ranges;
export import :vulkan.Gpu;
export import :vulkan.buffer.IndirectDrawCommands;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief GPU buffers for <tt>fastgltf::Scene</tt>.
     *
     * These buffers should be used for only the scene passed by the parameter. If you're finding the asset-wide buffers
     * (like materials, vertex/index buffers, primitives, etc.), see <tt>AssetGpuBuffers</tt> for that purpose.
     */
    export class AssetSceneGpuBuffers {
        const fastgltf::Asset *pAsset;

        std::vector<std::uint32_t> instanceCounts;
        std::vector<std::uint32_t> instanceOffsets = createInstanceOffsets();

    public:
        /**
         * @brief Buffer that stores the mesh nodes' transform matrices, with flattened instance matrices.
         *
         * The term "mesh node" means a node that has a mesh. This buffer only contains transform matrices of mesh nodes. In other words, <tt>meshNodeWorldTransformBuffer.asRange<const fastgltf::math::fmat4x4>()[nodeIndex]</tt> may NOT represent the world transformation matrix of the <tt>nodeIndex</tt>-th node, because maybe there were nodes with no mesh prior to the <tt>nodeIndex</tt>-th node.
         *
         * For example, a scene has 4 nodes (denoted as A B C D) and A has 2 instances (<tt>M1</tt>, <tt>M2</tt>), B has 3 instances (<tt>M3</tt>, <tt>M4</tt>, <tt>M5</tt>), C is meshless, and D has 1 instance (<tt>M6</tt>), then the flattened matrices will be laid out as:
         * @code
         * [MA * M1, MA * M2, MB * M3, MB * M4, MB * M5, MD * M6]
         * @endcode
         * Be careful that there is no transform matrix related about node C, because it is meshless.
         */
        vku::MappedBuffer meshNodeWorldTransformBuffer;

        /**
         * @brief Buffer that stores the start address of the flattened node world transform matrices buffer.
         */
        vku::AllocatedBuffer nodeBuffer;

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        AssetSceneGpuBuffers(
            const fastgltf::Asset &asset [[clang::lifetimebound]],
            const fastgltf::Scene &scene [[clang::lifetimebound]],
            const SceneNodeWorldTransforms &sceneNodeWorldTransforms,
            const vulkan::Gpu &gpu [[clang::lifetimebound]],
            const BufferDataAdapter &adapter = {}
        ) : pAsset { &asset },
            instanceCounts { createInstanceCounts(scene) },
            meshNodeWorldTransformBuffer { createMeshNodeWorldTransformBuffer(scene, sceneNodeWorldTransforms, gpu.allocator, adapter) },
            nodeBuffer { createNodeBuffer(gpu) } { }

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
        [[nodiscard]] const fastgltf::math::fmat4x4 &getMeshNodeWorldTransform(std::uint16_t nodeIndex, std::uint32_t instanceIndex = 0) const noexcept;

        /**
         * @brief Update the mesh node world transforms from given \p nodeIndex, to its descendants.
         * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view. If you provided <tt>fastgltf::Options::LoadExternalBuffers</tt> to the <tt>fastgltf::Parser</tt> while loading the glTF, the parameter can be omitted.
         * @param nodeIndex Node index to be started. The target node MUST have a mesh.
         * @param sceneNodeWorldTransforms Pre-calculated world transforms of the scene nodes.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void updateMeshNodeTransformsFrom(std::uint16_t nodeIndex, const SceneNodeWorldTransforms &sceneNodeWorldTransforms, const BufferDataAdapter &adapter = {}) {
            const std::span<fastgltf::math::fmat4x4> meshNodeWorldTransforms = meshNodeWorldTransformBuffer.asRange<fastgltf::math::fmat4x4>();
            algorithm::traverseNode(*pAsset, nodeIndex, [&](std::size_t nodeIndex) {
                const fastgltf::Node &node = pAsset->nodes[nodeIndex];
                if (!node.meshIndex) {
                    return;
                }

                if (std::vector instanceTransforms = getInstanceTransforms(*pAsset, node, adapter); instanceTransforms.empty()) {
                    meshNodeWorldTransforms[instanceOffsets[nodeIndex]] = sceneNodeWorldTransforms.worldTransforms[nodeIndex];
                }
                else {
                    for (std::uint32_t instanceIndex : ranges::views::upto(instanceCounts[nodeIndex])) {
                        meshNodeWorldTransforms[instanceOffsets[nodeIndex] + instanceIndex]
                            = sceneNodeWorldTransforms.worldTransforms[nodeIndex] * instanceTransforms[instanceIndex];
                    }
                }
            });
        }

        template <
            std::invocable<const AssetPrimitiveInfo&> CriteriaGetter,
            typename Criteria = std::invoke_result_t<CriteriaGetter, const AssetPrimitiveInfo&>,
            typename Compare = std::less<Criteria>>
        [[nodiscard]] auto createIndirectDrawCommandBuffers(
            vma::Allocator allocator,
            const CriteriaGetter &criteriaGetter,
            const std::unordered_set<std::uint16_t> &nodeIndices,
            concepts::compatible_signature_of<const AssetPrimitiveInfo&, const fastgltf::Primitive&> auto const &primitiveInfoGetter
        ) const -> std::map<Criteria, std::variant<vulkan::buffer::IndirectDrawCommands<false>, vulkan::buffer::IndirectDrawCommands<true>>, Compare> {
            std::map<Criteria, std::variant<std::vector<vk::DrawIndirectCommand>, std::vector<vk::DrawIndexedIndirectCommand>>> commandGroups;

            for (std::uint16_t nodeIndex : nodeIndices) {
                const fastgltf::Node &node = pAsset->nodes[nodeIndex];
                if (!node.meshIndex) {
                    continue;
                }

                // EXT_mesh_gpu_instancing support.
                std::uint32_t instanceCount = 1;
                if (!node.instancingAttributes.empty()) {
                    instanceCount = pAsset->accessors[node.instancingAttributes[0].accessorIndex].count;
                }

                const fastgltf::Mesh &mesh = pAsset->meshes[*node.meshIndex];
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    const AssetPrimitiveInfo &primitiveInfo = primitiveInfoGetter(primitive);
                    const Criteria criteria = criteriaGetter(primitiveInfo);
                    if (const auto &indexInfo = primitiveInfo.indexInfo) {
                        const std::size_t indexByteSize = [=]() {
                            switch (indexInfo->type) {
                                case vk::IndexType::eUint8KHR: return sizeof(std::uint8_t);
                                case vk::IndexType::eUint16: return sizeof(std::uint16_t);
                                case vk::IndexType::eUint32: return sizeof(std::uint32_t);
                                default: std::unreachable();
                            }
                        }();

                        auto &commandGroup = commandGroups
                            .try_emplace(criteria, std::in_place_type<std::vector<vk::DrawIndexedIndirectCommand>>)
                            .first->second;
                        const std::uint32_t firstIndex = static_cast<std::uint32_t>(primitiveInfo.indexInfo->offset / indexByteSize);
                        get_if<std::vector<vk::DrawIndexedIndirectCommand>>(&commandGroup)
                            ->emplace_back(
                                primitiveInfo.drawCount, instanceCount, firstIndex, 0,
                                (static_cast<std::uint32_t>(nodeIndex) << 16U) | static_cast<std::uint32_t>(primitiveInfo.index));
                    }
                    else {
                        auto &commandGroup = commandGroups
                            .try_emplace(criteria, std::in_place_type<std::vector<vk::DrawIndirectCommand>>)
                            .first->second;
                        get_if<std::vector<vk::DrawIndirectCommand>>(&commandGroup)
                            ->emplace_back(
                                primitiveInfo.drawCount, instanceCount, 0,
                                (static_cast<std::uint32_t>(nodeIndex) << 16U) | static_cast<std::uint32_t>(primitiveInfo.index));
                    }
                }
            }

            using result_type = std::variant<vulkan::buffer::IndirectDrawCommands<false>, vulkan::buffer::IndirectDrawCommands<true>>;
            return commandGroups
                | ranges::views::value_transform([allocator](const auto &variant) {
                    return visit(multilambda {
                        [allocator](std::span<const vk::DrawIndirectCommand> commands) {
                            return result_type {
                                std::in_place_type<vulkan::buffer::IndirectDrawCommands<false>>,
                                allocator,
                                commands,
                            };
                        },
                        [allocator](std::span<const vk::DrawIndexedIndirectCommand> commands) {
                            return result_type {
                                std::in_place_type<vulkan::buffer::IndirectDrawCommands<true>>,
                                allocator,
                                commands,
                            };
                        },
                    }, variant);
                })
                | std::ranges::to<std::map<Criteria, result_type, Compare>>();
        }

    private:
        [[nodiscard]] std::vector<std::uint32_t> createInstanceCounts(const fastgltf::Scene &scene) const;
        [[nodiscard]] std::vector<std::uint32_t> createInstanceOffsets() const;

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        [[nodiscard]] vku::MappedBuffer createMeshNodeWorldTransformBuffer(
            const fastgltf::Scene &scene,
            const SceneNodeWorldTransforms &sceneNodeWorldTransforms,
            vma::Allocator allocator,
            const BufferDataAdapter &adapter
        ) const {
            std::vector<fastgltf::math::fmat4x4> meshNodeWorldTransforms(instanceOffsets.back() + instanceCounts.back());
            algorithm::traverseScene(*pAsset, scene, [&](std::size_t nodeIndex) {
                const fastgltf::Node &node = pAsset->nodes[nodeIndex];
                if (!node.meshIndex) {
                    return;
                }

                if (std::vector instanceTransforms = getInstanceTransforms(*pAsset, node, adapter); instanceTransforms.empty()) {
                    meshNodeWorldTransforms[instanceOffsets[nodeIndex]] = sceneNodeWorldTransforms.worldTransforms[nodeIndex];
                }
                else {
                    for (std::uint32_t instanceIndex : ranges::views::upto(instanceCounts[nodeIndex])) {
                        meshNodeWorldTransforms[instanceOffsets[nodeIndex] + instanceIndex]
                            = sceneNodeWorldTransforms.worldTransforms[nodeIndex] * instanceTransforms[instanceIndex];
                    }
                }
            });

            return vku::MappedBuffer {
                allocator,
                std::from_range, as_bytes(std::span { meshNodeWorldTransforms }),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            };
        }

        [[nodiscard]] vku::AllocatedBuffer createNodeBuffer(const vulkan::Gpu &gpu) const;
    };
}