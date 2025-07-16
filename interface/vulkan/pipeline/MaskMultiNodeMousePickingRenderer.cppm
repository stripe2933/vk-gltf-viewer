module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.MaskMultiNodeMousePickingRenderer;

import std;
export import fastgltf;
import vku;

import vk_gltf_viewer.shader_selector.mask_multi_node_mouse_picking_frag;
import vk_gltf_viewer.shader_selector.mask_node_index_vert;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pl.MultiNodeMousePicking;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class MaskMultiNodeMousePickingRendererSpecialization {
    public:
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::optional<std::pair<fastgltf::ComponentType, bool>> baseColorTexcoordComponentTypeAndNormalized;
        std::optional<fastgltf::ComponentType> color0AlphaComponentType;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;
        bool useTextureTransform;

        [[nodiscard]] bool operator==(const MaskMultiNodeMousePickingRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const Gpu &gpu LIFETIMEBOUND,
            const pl::MultiNodeMousePicking &pipelineLayout LIFETIMEBOUND
        ) const;

    private:
        struct VertexShaderSpecializationData;
        struct FragmentShaderSpecializationData;

        [[nodiscard]] std::array<int, 2> getVertexShaderVariants() const noexcept;
        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const noexcept;
        [[nodiscard]] std::array<int, 2> getFragmentShaderVariants() const noexcept;
        [[nodiscard]] FragmentShaderSpecializationData getFragmentShaderSpecializationData() const noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

struct vk_gltf_viewer::vulkan::pipeline::MaskMultiNodeMousePickingRendererSpecialization::VertexShaderSpecializationData {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t baseColorTexcoordComponentType;
    vk::Bool32 baseColorTexcoordNormalized;
    std::uint32_t color0ComponentType;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

struct vk_gltf_viewer::vulkan::pipeline::MaskMultiNodeMousePickingRendererSpecialization::FragmentShaderSpecializationData {
    vk::Bool32 useTextureTransform;
};

vk::raii::Pipeline vk_gltf_viewer::vulkan::pipeline::MaskMultiNodeMousePickingRendererSpecialization::createPipeline(
    const Gpu &gpu,
    const pl::MultiNodeMousePicking &pipelineLayout
) const {
    return { gpu.device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                gpu.device,
                vku::Shader {
                    std::apply(LIFT(shader_selector::mask_node_index_vert), getVertexShaderVariants()),
                    vk::ShaderStageFlagBits::eVertex,
                    vku::unsafeAddress(vk::SpecializationInfo {
                        SpecializationMap<VertexShaderSpecializationData>::value,
                        vku::unsafeProxy(getVertexShaderSpecializationData()),
                    }),
                },
                vku::Shader {
                    std::apply(LIFT(shader_selector::mask_multi_node_mouse_picking_frag), getFragmentShaderVariants()),
                    vk::ShaderStageFlagBits::eFragment,
                    vku::unsafeAddress(vk::SpecializationInfo {
                        SpecializationMap<FragmentShaderSpecializationData>::value,
                        vku::unsafeProxy(getFragmentShaderSpecializationData()),
                    }),
                }).get(),
            // See doc about Gpu::Workaround::attachmentLessRenderPass.
            *pipelineLayout, 0, gpu.workaround.attachmentLessRenderPass)
            .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                {},
                topologyClass.value_or(vk::PrimitiveTopology::eTriangleList),
            }))
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eNone, {},
                false, false, false, false,
                1.f,
            }))
            .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                    vk::DynamicState::ePrimitiveTopology,
                }),
            })),
        vk::PipelineRenderingCreateInfo {
            {},
            {},
            gpu.workaround.attachmentLessRenderPass ? vk::Format::eD32Sfloat : vk::Format::eUndefined,
        },
    }.get() };
}

std::array<int, 2> vk_gltf_viewer::vulkan::pipeline::MaskMultiNodeMousePickingRendererSpecialization::getVertexShaderVariants() const noexcept {
    return {
        baseColorTexcoordComponentTypeAndNormalized.has_value(),
        color0AlphaComponentType.has_value(),
    };
}

vk_gltf_viewer::vulkan::pipeline::MaskMultiNodeMousePickingRendererSpecialization::VertexShaderSpecializationData vk_gltf_viewer::vulkan::pipeline::MaskMultiNodeMousePickingRendererSpecialization::getVertexShaderSpecializationData() const noexcept {
    VertexShaderSpecializationData result {
        .positionComponentType = getGLComponentType(positionComponentType),
        .positionNormalized = positionNormalized,
        .color0ComponentType = color0AlphaComponentType.transform(fastgltf::getGLComponentType).value_or(0U),
        .positionMorphTargetCount = positionMorphTargetCount,
        .skinAttributeCount = skinAttributeCount,
    };

    if (baseColorTexcoordComponentTypeAndNormalized) {
        result.baseColorTexcoordComponentType = getGLComponentType(baseColorTexcoordComponentTypeAndNormalized->first);
        result.baseColorTexcoordNormalized = baseColorTexcoordComponentTypeAndNormalized->second;
    }

    return result;
}

std::array<int, 2> vk_gltf_viewer::vulkan::pipeline::MaskMultiNodeMousePickingRendererSpecialization::getFragmentShaderVariants() const noexcept {
    return {
        baseColorTexcoordComponentTypeAndNormalized.has_value(),
        color0AlphaComponentType.has_value(),
    };
}

vk_gltf_viewer::vulkan::pipeline::MaskMultiNodeMousePickingRendererSpecialization::FragmentShaderSpecializationData vk_gltf_viewer::vulkan::pipeline::MaskMultiNodeMousePickingRendererSpecialization::getFragmentShaderSpecializationData() const noexcept {
    return { useTextureTransform };
}