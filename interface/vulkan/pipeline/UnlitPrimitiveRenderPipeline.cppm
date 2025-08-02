module;

#include <cassert>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.UnlitPrimitiveRenderPipeline;

import std;
export import fastgltf;
import vku;

import vk_gltf_viewer.shader_selector.unlit_primitive_frag;
import vk_gltf_viewer.shader_selector.unlit_primitive_vert;
export import vk_gltf_viewer.vulkan.pipeline_layout.Primitive;
export import vk_gltf_viewer.vulkan.render_pass.Scene;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class UnlitPrimitiveRenderPipeline final : public vk::raii::Pipeline {
    public:
        struct Config {
            std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
            fastgltf::ComponentType positionComponentType;
            bool positionNormalized;
            std::optional<std::pair<fastgltf::ComponentType, bool>> baseColorTexcoordComponentTypeAndNormalized;
            std::optional<std::pair<fastgltf::ComponentType, std::uint8_t>> color0ComponentTypeAndCount;
            std::uint32_t positionMorphTargetCount;
            std::uint32_t skinAttributeCount;
            bool useTextureTransform;
            fastgltf::AlphaMode alphaMode;

            [[nodiscard]] bool operator==(const Config&) const noexcept = default;
        };

        UnlitPrimitiveRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::Primitive &layout LIFETIMEBOUND,
            const rp::Scene &sceneRenderPass LIFETIMEBOUND,
            const Config &config
        );

    private:
        struct VertexShaderSpecialization;
        struct FragmentShaderSpecialization;

        [[nodiscard]] static std::array<int, 2> getVertexShaderVariants(const Config &config) noexcept;
        [[nodiscard]] static VertexShaderSpecialization getVertexShaderSpecialization(const Config &config) noexcept;
        [[nodiscard]] static std::array<int, 3> getFragmentShaderVariants(const Config &config) noexcept;
        [[nodiscard]] static FragmentShaderSpecialization getFragmentShaderSpecialization(const Config &config) noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

struct vk_gltf_viewer::vulkan::pipeline::UnlitPrimitiveRenderPipeline::VertexShaderSpecialization {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t baseColorTexcoordComponentType;
    vk::Bool32 baseColorTexcoordNormalized;
    std::uint32_t color0ComponentType;
    std::uint32_t color0ComponentCount;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

struct vk_gltf_viewer::vulkan::pipeline::UnlitPrimitiveRenderPipeline::FragmentShaderSpecialization {
    vk::Bool32 useTextureTransform;
};

vk_gltf_viewer::vulkan::pipeline::UnlitPrimitiveRenderPipeline::UnlitPrimitiveRenderPipeline(
    const vk::raii::Device &device,
    const pl::Primitive &layout,
    const rp::Scene &sceneRenderPass,
    const Config &config
) : Pipeline { [&] -> Pipeline {
        const auto vertexShaderSpecialization = getVertexShaderSpecialization(config);
        const vk::SpecializationInfo vertexShaderSpecializationInfo {
            SpecializationMap<VertexShaderSpecialization>::value,
            vk::ArrayProxyNoTemporaries<const VertexShaderSpecialization> { vertexShaderSpecialization },
        };

        const auto fragmentShaderSpecialization = getFragmentShaderSpecialization(config);
        const vk::SpecializationInfo fragmentShaderSpecializationInfo {
            SpecializationMap<FragmentShaderSpecialization>::value,
            vk::ArrayProxyNoTemporaries<const FragmentShaderSpecialization> { fragmentShaderSpecialization },
        };

        const vku::RefHolder pipelineStages = createPipelineStages(
            device,
            vku::Shader {
                std::apply(LIFT(shader_selector::unlit_primitive_vert), getVertexShaderVariants(config)),
                vk::ShaderStageFlagBits::eVertex,
                &vertexShaderSpecializationInfo
            },
            vku::Shader {
                std::apply(LIFT(shader_selector::unlit_primitive_frag), getFragmentShaderVariants(config)),
                vk::ShaderStageFlagBits::eFragment,
                &fragmentShaderSpecializationInfo,
            });

        const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo {
            {},
            config.topologyClass.value_or(vk::PrimitiveTopology::eTriangleList),
        };

        switch (config.alphaMode) {
            case fastgltf::AlphaMode::Opaque:
                return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                    pipelineStages.get(), *layout, 1, true, vk::SampleCountFlagBits::e4)
                    .setPInputAssemblyState(&inputAssemblyStateCreateInfo)
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
                    .setRenderPass(*sceneRenderPass)
                    .setSubpass(0)
                };
            case fastgltf::AlphaMode::Mask:
                return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                    pipelineStages.get(), *layout, 1, true, vk::SampleCountFlagBits::e4)
                    .setPInputAssemblyState(&inputAssemblyStateCreateInfo)
                    .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                        {},
                        true, true, vk::CompareOp::eGreater, // Use reverse Z.
                    }))
                    .setPMultisampleState(vku::unsafeAddress(vk::PipelineMultisampleStateCreateInfo {
                        {},
                        vk::SampleCountFlagBits::e4,
                        {}, {}, {},
                        true,
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
                    .setRenderPass(*sceneRenderPass)
                    .setSubpass(0)
                };
            case fastgltf::AlphaMode::Blend:
                return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                    pipelineStages.get(), *layout, 1, true, vk::SampleCountFlagBits::e4)
                    .setPInputAssemblyState(&inputAssemblyStateCreateInfo)
                    .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                        {},
                        false, false,
                        vk::PolygonMode::eFill,
                        // Translucent objects' back faces shouldn't be culled.
                        vk::CullModeFlagBits::eNone, {},
                        false, {}, {}, {},
                        1.f,
                    }))
                    .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                        {},
                        // Translucent objects shouldn't interfere with the pre-rendered depth buffer. Use reverse Z.
                        true, false, vk::CompareOp::eGreater,
                    }))
                    .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                        {},
                        false, {},
                        vku::unsafeProxy({
                            vk::PipelineColorBlendAttachmentState {
                                true,
                                vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                                vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                            },
                            vk::PipelineColorBlendAttachmentState {
                                true,
                                vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcColor, vk::BlendOp::eAdd,
                                vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                                vk::ColorComponentFlagBits::eR,
                            },
                        }),
                        { 1.f, 1.f, 1.f, 1.f },
                    }))
                    .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                        {},
                        vku::unsafeProxy({
                            vk::DynamicState::eViewport,
                            vk::DynamicState::eScissor,
                            vk::DynamicState::ePrimitiveTopology,
                        }),
                    }))
                    .setRenderPass(*sceneRenderPass)
                    .setSubpass(1)
                };
        }
        std::unreachable();
    }() } { }

std::array<int, 2> vk_gltf_viewer::vulkan::pipeline::UnlitPrimitiveRenderPipeline::getVertexShaderVariants(const Config &config) noexcept {
    return {
        config.baseColorTexcoordComponentTypeAndNormalized.has_value(),
        config.color0ComponentTypeAndCount.has_value(),
    };
}

vk_gltf_viewer::vulkan::pipeline::UnlitPrimitiveRenderPipeline::VertexShaderSpecialization vk_gltf_viewer::vulkan::pipeline::UnlitPrimitiveRenderPipeline::getVertexShaderSpecialization(const Config &config) noexcept {
    VertexShaderSpecialization result {
        .positionComponentType = getGLComponentType(config.positionComponentType),
        .positionNormalized = config.positionNormalized,
        .positionMorphTargetCount = config.positionMorphTargetCount,
        .skinAttributeCount = config.skinAttributeCount,
    };

    if (config.baseColorTexcoordComponentTypeAndNormalized) {
        result.baseColorTexcoordComponentType = getGLComponentType(config.baseColorTexcoordComponentTypeAndNormalized->first);
        result.baseColorTexcoordNormalized = config.baseColorTexcoordComponentTypeAndNormalized->second;
    }

    if (config.color0ComponentTypeAndCount) {
        result.color0ComponentType = getGLComponentType(config.color0ComponentTypeAndCount->first);
        result.color0ComponentCount = config.color0ComponentTypeAndCount->second;
    }

    return result;
}

std::array<int, 3> vk_gltf_viewer::vulkan::pipeline::UnlitPrimitiveRenderPipeline::getFragmentShaderVariants(const Config &config) noexcept {
    return {
        config.baseColorTexcoordComponentTypeAndNormalized.has_value(),
        config.color0ComponentTypeAndCount.has_value(),
        static_cast<int>(config.alphaMode),
    };
}

vk_gltf_viewer::vulkan::pipeline::UnlitPrimitiveRenderPipeline::FragmentShaderSpecialization vk_gltf_viewer::vulkan::pipeline::UnlitPrimitiveRenderPipeline::getFragmentShaderSpecialization(const Config &config) noexcept {
    return { config.useTextureTransform };
}