module;

#include <cstdint>
#include <compare>
#include <map>
#include <optional>

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:gltf.SceneResources;

export import glm;
export import vku;
export import :gltf.AssetResources;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    export class SceneResources {
    public:
        struct GpuPrimitive {
            vk::DeviceAddress pPositionBuffer,
                              pNormalBuffer,
                              pTangentBuffer,
                              pTexcoordBufferPtrsBuffer,
                              pColorBufferPtrsBuffer;
            std::uint8_t      positionByteStride,
                              normalByteStride,
                              tangentByteStride;
            char              padding[5];
            vk::DeviceAddress pTexcoordByteStridesBuffer,
                              pColorByteStridesBuffer;
            std::uint32_t     nodeIndex,
                              materialIndex;
        };

        struct CommandSeparationCriteria {
            fastgltf::AlphaMode alphaMode;
            bool doubleSided;
            std::optional<vk::IndexType> indexType;

            constexpr auto operator<=>(const CommandSeparationCriteria &) const noexcept -> std::strong_ordering = default;
        };

        struct CommandSeparationCriteriaComparator {
            using is_transparent = void;

            static auto operator()(const CommandSeparationCriteria &lhs, const CommandSeparationCriteria &rhs) noexcept -> bool { return lhs < rhs; }
            static auto operator()(const CommandSeparationCriteria &lhs, fastgltf::AlphaMode rhs) noexcept -> bool { return lhs.alphaMode < rhs; }
            static auto operator()(fastgltf::AlphaMode lhs, const CommandSeparationCriteria &rhs) noexcept -> bool { return lhs < rhs.alphaMode; }
        };

        const AssetResources &assetResources;
        const fastgltf::Scene &scene;

        std::vector<std::pair<std::uint32_t /* nodeIndex */, const AssetResources::PrimitiveInfo*>> orderedNodePrimitiveInfoPtrs = createOrderedNodePrimitiveInfoPtrs();

        vku::MappedBuffer nodeTransformBuffer;
        vku::MappedBuffer primitiveBuffer;
        std::map<CommandSeparationCriteria, vku::MappedBuffer, CommandSeparationCriteriaComparator> indirectDrawCommandBuffers;

        SceneResources(const AssetResources &assetResources, const fastgltf::Scene &scene, const vulkan::Gpu &gpu);

    private:
        [[nodiscard]] auto createOrderedNodePrimitiveInfoPtrs() const -> decltype(orderedNodePrimitiveInfoPtrs);
        [[nodiscard]] auto createNodeTransformBuffer(vma::Allocator allocator) const -> decltype(nodeTransformBuffer);
        [[nodiscard]] auto createPrimitiveBuffer(const vulkan::Gpu &gpu) -> decltype(primitiveBuffer);
        [[nodiscard]] auto createIndirectDrawCommandBuffer(const fastgltf::Asset &asset, vma::Allocator allocator) const -> decltype(indirectDrawCommandBuffers);
    };
}