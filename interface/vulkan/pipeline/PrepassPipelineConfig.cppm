export module vk_gltf_viewer.vulkan.pipeline.PrepassPipelineConfig;

import std;
export import fastgltf;
export import vku;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export template <bool Mask>
    struct PrepassPipelineConfig;

    export template <>
    struct PrepassPipelineConfig<false> {
        std::optional<vku::TopologyClass> topologyClass;
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;

        [[nodiscard]] std::strong_ordering operator<=>(const PrepassPipelineConfig&) const noexcept = default;
    };

    export template <>
    struct PrepassPipelineConfig<true> {
        std::optional<vku::TopologyClass> topologyClass;
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