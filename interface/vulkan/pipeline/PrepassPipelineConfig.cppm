export module vk_gltf_viewer.vulkan.pipeline.PrepassPipelineConfig;

import std;
export import fastgltf;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export template <bool Mask>
    struct PrepassPipelineConfig;

    export template <>
    struct PrepassPipelineConfig<false> {
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;

        [[nodiscard]] std::strong_ordering operator<=>(const PrepassPipelineConfig&) const noexcept = default;
    };

    export template <>
    struct PrepassPipelineConfig<true> {
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::optional<std::pair<fastgltf::ComponentType, bool>> baseColorTexcoordComponentTypeAndNormalized;
        std::optional<fastgltf::ComponentType> color0AlphaComponentType;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;
        bool useTextureTransform;

        [[nodiscard]] std::strong_ordering operator<=>(const PrepassPipelineConfig&) const noexcept = default;
    };
}