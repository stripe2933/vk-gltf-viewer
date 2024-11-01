export module vk_gltf_viewer:gltf.AssetSceneGpuBuffers;

import std;
export import fastgltf;
export import :gltf.AssetPrimitiveInfo;
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
    public:
        /**
         * @brief Asset primitives that are ordered by preorder scene traversal.
         *
         * It is a flattened list of (node index, &primitive) pairs that are collected by the preorder traversal of scene.
         * Since a mesh has multiple primitives, two consecutive pairs may have the same node index.
         */
        std::vector<std::pair<std::size_t /* nodeIndex */, const fastgltf::Primitive*>> orderedNodePrimitives;

        /**
         * @brief Buffer that contains world transformation matrices of each node.
         *
         * <tt>nodeWorldTransformBuffer.asRange<const glm::mat4>()[i]</tt> represents the world transformation matrix of the <tt>i</tt>-th node.
         */
        vku::MappedBuffer nodeWorldTransformBuffer;

        AssetSceneGpuBuffers(
            const fastgltf::Asset &asset [[clang::lifetimebound]],
            const fastgltf::Scene &scene [[clang::lifetimebound]],
            const vulkan::Gpu &gpu [[clang::lifetimebound]]);

        template <
            std::invocable<const AssetPrimitiveInfo&> CriteriaGetter,
            typename Compare = std::less<CriteriaGetter>,
            typename Criteria = std::invoke_result_t<CriteriaGetter, const AssetPrimitiveInfo&>>
        requires
            requires(const Criteria &criteria) {
                // Draw commands with same criteria must have same kind of index type, or no index type (multi draw
                // indirect requires the same index type).
                { criteria.indexType } -> std::convertible_to<std::optional<vk::IndexType>>;
            }
        [[nodiscard]] auto createIndirectDrawCommandBuffers(
            vma::Allocator allocator,
            const CriteriaGetter &criteriaGetter,
            const std::unordered_set<std::size_t> &nodeIndices,
            std::invocable<const fastgltf::Primitive&> auto const &primitiveInfoGetter
        ) const -> std::map<Criteria, std::variant<vulkan::buffer::IndirectDrawCommands<false>, vulkan::buffer::IndirectDrawCommands<true>>, Compare> {
            std::map<Criteria, std::variant<std::vector<vk::DrawIndirectCommand>, std::vector<vk::DrawIndexedIndirectCommand>>> commandGroups;

            for (auto [nodeIndex, pPrimitive] : orderedNodePrimitives) {
                if (!nodeIndices.contains(nodeIndex)) {
                    continue;
                }

                const AssetPrimitiveInfo &primitiveInfo = primitiveInfoGetter(*pPrimitive);
                const Criteria criteria = criteriaGetter(primitiveInfo);
                if (const auto &indexInfo = primitiveInfo.indexInfo) {
                    const std::size_t indexByteSize = [=]() {
                        switch (indexInfo->type) {
                            case vk::IndexType::eUint8KHR: return sizeof(std::uint8_t);
                            case vk::IndexType::eUint16: return sizeof(std::uint16_t);
                            case vk::IndexType::eUint32: return sizeof(std::uint32_t);
                            default: throw std::runtime_error{ "Unsupported index type: only Uint8KHR, Uint16 and Uint32 are supported." };
                        }
                    }();

                    auto &commandGroup = commandGroups
                        .try_emplace(criteria, std::in_place_type<std::vector<vk::DrawIndexedIndirectCommand>>)
                        .first->second;
                    const std::uint32_t firstIndex = static_cast<std::uint32_t>(primitiveInfo.indexInfo->offset / indexByteSize);
                    get_if<std::vector<vk::DrawIndexedIndirectCommand>>(&commandGroup)
                        ->emplace_back(primitiveInfo.drawCount, 1, firstIndex, 0, (nodeIndex << 16U) | primitiveInfo.index);
                }
                else {
                    auto &commandGroup = commandGroups
                        .try_emplace(criteria, std::in_place_type<std::vector<vk::DrawIndirectCommand>>)
                        .first->second;
                    get_if<std::vector<vk::DrawIndirectCommand>>(&commandGroup)
                        ->emplace_back(primitiveInfo.drawCount, 1, 0, (nodeIndex << 16U) | primitiveInfo.index);
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
        [[nodiscard]] std::vector<std::pair<std::size_t, const fastgltf::Primitive*>> createOrderedNodePrimitives(const fastgltf::Asset &asset, const fastgltf::Scene &scene) const;
        [[nodiscard]] vku::MappedBuffer createNodeWorldTransformBuffer(const fastgltf::Asset &asset, const fastgltf::Scene &scene, vma::Allocator allocator) const;
    };
}