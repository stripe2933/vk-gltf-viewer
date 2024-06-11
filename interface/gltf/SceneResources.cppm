module;

#include <compare>
#include <map>
#include <optional>

#include <fastgltf/core.hpp>

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
            std::optional<vk::IndexType> indexType;
            bool isDoubleSided;

            [[nodiscard]] constexpr auto operator<=>(const CommandSeparationCriteria &) const noexcept -> std::strong_ordering = default;
        };

        const AssetResources &assetResources;
        const fastgltf::Scene &scene;

        vku::MappedBuffer nodeTransformBuffer;
        vku::MappedBuffer primitiveBuffer;
        /*std::map<CommandSeparationCriteria, vku::MappedBuffer> indirectDrawCommandBuffers;*/

        SceneResources(const AssetResources &assetResources, const fastgltf::Scene &scene, const vulkan::Gpu &gpu);

    private:
        [[nodiscard]] auto createNodeTransformBuffer(const vulkan::Gpu &gpu) const -> decltype(nodeTransformBuffer);
        [[nodiscard]] auto createPrimitiveBuffer(const vulkan::Gpu &gpu) -> decltype(primitiveBuffer);
        /*[[nodiscard]] auto createIndirectDrawCommandBuffer(const AssetResources &assetResources, const vulkan::Gpu &gpu) const -> decltype(indirectDrawCommandBuffers);*/
    };
}