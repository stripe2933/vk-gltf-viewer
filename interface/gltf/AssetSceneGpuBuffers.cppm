export module vk_gltf_viewer:gltf.AssetSceneGpuBuffers;

import std;
export import fastgltf;
export import :gltf.AssetPrimitiveInfo;
import :helpers.concepts;
import :helpers.functional;
export import :vulkan.Gpu;
export import :vulkan.buffer.IndirectDrawCommands;
export import :vulkan.buffer.MeshNodeWorldTransforms;
export import :vulkan.buffer.MeshWeights;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief GPU buffers for <tt>fastgltf::Scene</tt>.
     *
     * These buffers should be used for only the scene passed by the parameter. If you're finding the asset-wide buffers
     * (like materials, vertex/index buffers, primitives, etc.), see <tt>AssetGpuBuffers</tt> for that purpose.
     */
    export class AssetSceneGpuBuffers {
        const fastgltf::Asset *pAsset;

    public:
        /**
         * @brief Buffer that stores the start address of the flattened node world transform matrices buffer.
         */
        vku::AllocatedBuffer nodeBuffer;

        AssetSceneGpuBuffers(
            const fastgltf::Asset &asset [[clang::lifetimebound]],
            const vulkan::buffer::MeshNodeWorldTransforms &meshNodeWorldTransforms [[clang::lifetimebound]],
            const vulkan::buffer::MeshWeights &meshWeights [[clang::lifetimebound]],
            const vulkan::Gpu &gpu [[clang::lifetimebound]]
        ) : pAsset { &asset },
            nodeBuffer { createNodeBuffer(asset, meshNodeWorldTransforms, meshWeights, gpu) } { }

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
        [[nodiscard]] vku::AllocatedBuffer createNodeBuffer(
            const fastgltf::Asset &asset,
            const vulkan::buffer::MeshNodeWorldTransforms &meshNodeWorldTransforms,
            const vulkan::buffer::MeshWeights &meshWeights,
            const vulkan::Gpu &gpu
        ) const;
    };
}