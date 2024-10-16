module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:gltf.SceneResources;

import std;
export import :gltf.AssetResources;
import :helpers.functional;
import :helpers.ranges;
export import :vulkan.Gpu;
export import :vulkan.buffer.IndirectDrawCommands;

namespace vk_gltf_viewer::gltf {
    export class SceneResources {
        const fastgltf::Asset &asset;
        const vulkan::Gpu &gpu;
        const AssetResources &assetResources;
        const fastgltf::Scene &scene;

    public:
        struct GpuPrimitive {
            vk::DeviceAddress pPositionBuffer;
            vk::DeviceAddress pNormalBuffer;
            vk::DeviceAddress pTangentBuffer;
            vk::DeviceAddress pTexcoordAttributeMappingInfoBuffer;
            vk::DeviceAddress pColorAttributeMappingInfoBuffer;
            std::uint8_t positionByteStride;
            std::uint8_t normalByteStride;
            std::uint8_t tangentByteStride;
            char padding[1];
            std::uint32_t nodeIndex;
            std::int32_t materialIndex; // -1 for fallback material.
        };

        std::vector<std::pair<std::size_t /* nodeIndex */, const AssetResources::PrimitiveInfo*>> orderedNodePrimitiveInfoPtrs = createOrderedNodePrimitiveInfoPtrs();
        vku::MappedBuffer nodeWorldTransformBuffer = createNodeWorldTransformBuffer();
        vku::AllocatedBuffer primitiveBuffer = createPrimitiveBuffer();

        /**
         * The smallest enclosing sphere of the scene meshes' bounding boxes, i.e. tight-fitting sphere.
         * First element is the center coordinate, and the second element is the radius.
         */
        std::pair<glm::dvec3, double> enclosingSphere = getEnclosingSphere();

        SceneResources(
            const fastgltf::Asset &asset [[clang::lifetimebound]],
            const AssetResources &assetResources [[clang::lifetimebound]],
            const fastgltf::Scene &scene [[clang::lifetimebound]],
            const vulkan::Gpu &gpu [[clang::lifetimebound]]);

        template <
            typename CriteriaGetter,
            typename Compare = std::less<CriteriaGetter>,
            typename Criteria = std::invoke_result_t<CriteriaGetter, const AssetResources::PrimitiveInfo&>>
        requires
            requires(const Criteria &criteria) {
                // Draw commands with same criteria must have same kind of index type, or no index type (multi draw
                // indirect requires the same index type).
                { criteria.indexType } -> std::convertible_to<std::optional<vk::IndexType>>;
            }
        [[nodiscard]] auto createIndirectDrawCommandBuffers(
            const CriteriaGetter &criteriaGetter,
            const std::unordered_set<std::size_t> &nodeIndices
        ) const -> std::map<Criteria, std::variant<vulkan::buffer::IndirectDrawCommands<false>, vulkan::buffer::IndirectDrawCommands<true>>, Compare> {
            std::map<Criteria, std::variant<std::vector<vk::DrawIndirectCommand>, std::vector<vk::DrawIndexedIndirectCommand>>> commandGroups;

            for (auto [primitiveIndex, nodePrimitiveInfo] : orderedNodePrimitiveInfoPtrs | ranges::views::enumerate) {
                const auto [nodeIndex, pPrimitiveInfo] = nodePrimitiveInfo;
                if (!nodeIndices.contains(nodeIndex)) {
                    continue;
                }

                const Criteria criteria = criteriaGetter(*pPrimitiveInfo);
                if (const auto &indexInfo = pPrimitiveInfo->indexInfo) {
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
                    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(pPrimitiveInfo->indexInfo->offset / indexByteSize);
                    get_if<std::vector<vk::DrawIndexedIndirectCommand>>(&commandGroup)
                        ->emplace_back(pPrimitiveInfo->drawCount, 1, vertexOffset, 0, primitiveIndex);
                }
                else {
                    auto &commandGroup = commandGroups
                        .try_emplace(criteria, std::in_place_type<std::vector<vk::DrawIndirectCommand>>)
                        .first->second;
                    get_if<std::vector<vk::DrawIndirectCommand>>(&commandGroup)
                        ->emplace_back(pPrimitiveInfo->drawCount, 1, 0, primitiveIndex);
                }
            }

            using result_type = std::variant<vulkan::buffer::IndirectDrawCommands<false>, vulkan::buffer::IndirectDrawCommands<true>>;
            return commandGroups
                | ranges::views::value_transform([allocator = gpu.allocator](const auto &variant) {
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

        /**
         * @brief Update the enclosing sphere of the scene meshes' bounding boxes based on the current nodes' world transforms.
         */
        void updateEnclosingSphere() {
            enclosingSphere = getEnclosingSphere();
        }

    private:
        [[nodiscard]] auto createOrderedNodePrimitiveInfoPtrs() const -> std::vector<std::pair<std::size_t, const AssetResources::PrimitiveInfo*>>;
        [[nodiscard]] auto createNodeWorldTransformBuffer() const -> vku::MappedBuffer;
        [[nodiscard]] auto createPrimitiveBuffer() const -> vku::AllocatedBuffer;
        [[nodiscard]] auto getEnclosingSphere() const -> std::pair<glm::dvec3, double>;
    };
}