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
            std::optional<vku::TopologyClass> topologyClass;
            fastgltf::ComponentType positionComponentType;
            bool positionNormalized;
            std::optional<std::pair<fastgltf::ComponentType, bool>> baseColorTexcoordComponentTypeAndNormalized;
            std::optional<std::pair<fastgltf::ComponentType, std::uint8_t>> color0ComponentTypeAndCount;
            std::uint32_t positionMorphTargetCount;
            std::uint32_t skinAttributeCount;
            bool useTextureTransform;
            fastgltf::AlphaMode alphaMode;

            [[nodiscard]] std::strong_ordering operator<=>(const Config&) const noexcept = default;
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

        const vk::raii::ShaderModule vertexShaderModule { device, vk::ShaderModuleCreateInfo {
            {},
            vku::lvalue(std::apply(LIFT(shader_selector::unlit_primitive_vert), getVertexShaderVariants(config))),
        } };
        const vk::raii::ShaderModule fragmentShaderModule { device, vk::ShaderModuleCreateInfo {
            {},
            vku::lvalue(std::apply(LIFT(shader_selector::unlit_primitive_frag), getFragmentShaderVariants(config))),
        } };

        const std::array pipelineShaderStageCreateInfos {
            vk::PipelineShaderStageCreateInfo {
                {},
                vk::ShaderStageFlagBits::eVertex,
                *vertexShaderModule,
                "main",
                &vertexShaderSpecializationInfo
            },
            vk::PipelineShaderStageCreateInfo {
                {},
                vk::ShaderStageFlagBits::eFragment,
                *fragmentShaderModule,
                "main",
                &fragmentShaderSpecializationInfo,
            },
        };

        constexpr vk::PipelineVertexInputStateCreateInfo vertexInputStateCreateInfo{};
        const auto inputAssemblyStateCreateInfo = vku::defaultPipelineInputAssemblyState(vku::getListPrimitiveTopology(config.topologyClass.value_or(vku::TopologyClass::eTriangle)));
        constexpr vk::PipelineViewportStateCreateInfo viewportStateCreateInfo {
            {},
            1, nullptr,
            1, nullptr,
        };
        auto rasterizationStateCreateInfo = vku::defaultPipelineRasterizationState({}, vk::CullModeFlagBits::eBack);
        vk::PipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo { {}, true, true, vk::CompareOp::eGreater }; // Use reverse Z.
        vk::PipelineMultisampleStateCreateInfo multisampleStateCreateInfo { {}, vk::SampleCountFlagBits::e4 };

        constexpr auto opaqueColorBlendStateCreateInfo = vku::defaultPipelineColorBlendState(1);

        constexpr std::array translucentColorBlendAttachmentStates {
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
        };
        const vk::PipelineColorBlendStateCreateInfo translucentColorBlendStateCreateInfo {
            {},
            false, {},
            translucentColorBlendAttachmentStates,
            { 1.f, 1.f, 1.f, 1.f },
        };

        constexpr std::array dynamicStates {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
            vk::DynamicState::ePrimitiveTopology,
            vk::DynamicState::eCullMode,
        };
        vk::PipelineDynamicStateCreateInfo dynamicStateCreateInfo { {}, dynamicStates };

        vk::GraphicsPipelineCreateInfo createInfo {
            {},
            pipelineShaderStageCreateInfos,
            &vertexInputStateCreateInfo,
            &inputAssemblyStateCreateInfo,
            nullptr,
            &viewportStateCreateInfo,
            &rasterizationStateCreateInfo,
            &multisampleStateCreateInfo,
            &depthStencilStateCreateInfo,
            nullptr, // Set later based on alpha mode.
            &dynamicStateCreateInfo,
            *layout,
            *sceneRenderPass,
        };

        switch (config.alphaMode) {
            case fastgltf::AlphaMode::Opaque:
                createInfo.pColorBlendState = &opaqueColorBlendStateCreateInfo;
                createInfo.subpass = 0;
                break;
            case fastgltf::AlphaMode::Mask:
                multisampleStateCreateInfo.alphaToCoverageEnable = true;
                createInfo.pColorBlendState = &opaqueColorBlendStateCreateInfo;
                createInfo.subpass = 0;
                break;
            case fastgltf::AlphaMode::Blend:
                // Translucent objects' back faces shouldn't be culled.
                rasterizationStateCreateInfo.cullMode = vk::CullModeFlagBits::eNone;

                static_assert(dynamicStates.size() == 4 && dynamicStates[3] == vk::DynamicState::eCullMode, "dynamicStates modified");
                dynamicStateCreateInfo.dynamicStateCount = 3;

                // Translucent objects shouldn't interfere with the pre-rendered depth buffer.
                depthStencilStateCreateInfo.depthWriteEnable = false;

                createInfo.pColorBlendState = &translucentColorBlendStateCreateInfo;
                createInfo.subpass = 1;
                break;
        }

        return { device, nullptr, createInfo };
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