module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.MaskNodeIndexRenderPipeline;

import std;
import vku;

import vk_gltf_viewer.shader_selector.mask_node_index_frag;
import vk_gltf_viewer.shader_selector.mask_node_index_vert;
export import vk_gltf_viewer.vulkan.pipeline.PrepassPipelineConfig;
export import vk_gltf_viewer.vulkan.pipeline_layout.PrimitiveNoShading;
export import vk_gltf_viewer.vulkan.render_pass.MousePicking;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class MaskNodeIndexRenderPipeline final : public vk::raii::Pipeline {
    public:
        MaskNodeIndexRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::PrimitiveNoShading &pipelineLayout LIFETIMEBOUND,
            const rp::MousePicking &renderPass LIFETIMEBOUND,
            const PrepassPipelineConfig<true> &config
        );

    private:
        struct VertexShaderSpecialization;
        struct FragmentShaderSpecialization;

        [[nodiscard]] static std::array<int, 2> getVertexShaderVariants(const PrepassPipelineConfig<true> &config) noexcept;
        [[nodiscard]] static VertexShaderSpecialization getVertexShaderSpecialization(const PrepassPipelineConfig<true> &config) noexcept;
        [[nodiscard]] static std::array<int, 2> getFragmentShaderVariants(const PrepassPipelineConfig<true> &config) noexcept;
        [[nodiscard]] static FragmentShaderSpecialization getFragmentShaderSpecialization(const PrepassPipelineConfig<true> &config) noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

struct vk_gltf_viewer::vulkan::pipeline::MaskNodeIndexRenderPipeline::VertexShaderSpecialization {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t baseColorTexcoordComponentType;
    vk::Bool32 baseColorTexcoordNormalized;
    std::uint32_t color0ComponentType;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

struct vk_gltf_viewer::vulkan::pipeline::MaskNodeIndexRenderPipeline::FragmentShaderSpecialization {
    vk::Bool32 useTextureTransform;
};

vk_gltf_viewer::vulkan::pipeline::MaskNodeIndexRenderPipeline::MaskNodeIndexRenderPipeline(
    const vk::raii::Device &device,
    const pl::PrimitiveNoShading &pipelineLayout,
    const rp::MousePicking &renderPass,
    const PrepassPipelineConfig<true> &config
) : Pipeline { [&] -> Pipeline {
        return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader {
                    std::apply(LIFT(shader_selector::mask_node_index_vert), getVertexShaderVariants(config)),
                    vk::ShaderStageFlagBits::eVertex,
                    vku::unsafeAddress(vk::SpecializationInfo {
                        SpecializationMap<VertexShaderSpecialization>::value,
                        vku::unsafeProxy(getVertexShaderSpecialization(config)),
                    }),
                },
                vku::Shader {
                    std::apply(LIFT(shader_selector::mask_node_index_frag), getFragmentShaderVariants(config)),
                    vk::ShaderStageFlagBits::eFragment,
                    vku::unsafeAddress(vk::SpecializationInfo {
                        SpecializationMap<FragmentShaderSpecialization>::value,
                        vku::unsafeProxy(getFragmentShaderSpecialization(config)),
                    }),
                }).get(),
            *pipelineLayout, 1, true)
            .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                {},
                config.topologyClass.value_or(vk::PrimitiveTopology::eTriangleList),
            }))
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, true, vk::CompareOp::eGreater, // Use reverse Z.
            }))
            .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                    vk::DynamicState::ePrimitiveTopology,
                    vk::DynamicState::eCullMode,
                }),
            }))
            .setRenderPass(*renderPass)
            .setSubpass(0),
        };
    }() } { }

std::array<int, 2> vk_gltf_viewer::vulkan::pipeline::MaskNodeIndexRenderPipeline::getVertexShaderVariants(const PrepassPipelineConfig<true> &config) noexcept {
    return {
        config.baseColorTexcoordComponentTypeAndNormalized.has_value(),
        config.color0AlphaComponentType.has_value(),
    };
}

vk_gltf_viewer::vulkan::pipeline::MaskNodeIndexRenderPipeline::VertexShaderSpecialization vk_gltf_viewer::vulkan::pipeline::MaskNodeIndexRenderPipeline::getVertexShaderSpecialization(const PrepassPipelineConfig<true> &config) noexcept {
    VertexShaderSpecialization result {
        .positionComponentType = getGLComponentType(config.positionComponentType),
        .positionNormalized = config.positionNormalized,
        .color0ComponentType = config.color0AlphaComponentType.transform(fastgltf::getGLComponentType).value_or(0U),
        .positionMorphTargetCount = config.positionMorphTargetCount,
        .skinAttributeCount = config.skinAttributeCount,
    };

    if (config.baseColorTexcoordComponentTypeAndNormalized) {
        result.baseColorTexcoordComponentType = getGLComponentType(config.baseColorTexcoordComponentTypeAndNormalized->first);
        result.baseColorTexcoordNormalized = config.baseColorTexcoordComponentTypeAndNormalized->second;
    }

    return result;
}

std::array<int, 2> vk_gltf_viewer::vulkan::pipeline::MaskNodeIndexRenderPipeline::getFragmentShaderVariants(const PrepassPipelineConfig<true> &config) noexcept {
    return {
        config.baseColorTexcoordComponentTypeAndNormalized.has_value(),
        config.color0AlphaComponentType.has_value(),
    };
}

vk_gltf_viewer::vulkan::pipeline::MaskNodeIndexRenderPipeline::FragmentShaderSpecialization vk_gltf_viewer::vulkan::pipeline::MaskNodeIndexRenderPipeline::getFragmentShaderSpecialization(const PrepassPipelineConfig<true> &config) noexcept {
    return { config.useTextureTransform };
}