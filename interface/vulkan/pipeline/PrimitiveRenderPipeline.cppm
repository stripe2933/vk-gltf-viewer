module;

#include <boost/container/static_vector.hpp>
#include <boost/container_hash/hash.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.PrimitiveRenderPipeline;

import std;
export import fastgltf;

import vk_gltf_viewer.shader_selector.primitive_frag;
import vk_gltf_viewer.shader_selector.primitive_vert;
export import vk_gltf_viewer.vulkan.pipeline_layout.Primitive;
export import vk_gltf_viewer.vulkan.render_pass.Scene;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class PrimitiveRenderPipeline final : public vk::raii::Pipeline {
    public:
        struct Config {
            std::optional<vku::TopologyClass> topologyClass;
            fastgltf::ComponentType positionComponentType;
            bool positionNormalized;
            std::optional<fastgltf::ComponentType> normalComponentType;
            std::optional<fastgltf::ComponentType> tangentComponentType;
            boost::container::static_vector<std::pair<fastgltf::ComponentType, bool>, 4> texcoordComponentTypeAndNormalized;
            std::optional<std::pair<fastgltf::ComponentType, std::uint8_t>> color0ComponentTypeAndCount;
            std::uint32_t positionMorphTargetCount;
            std::uint32_t normalMorphTargetCount;
            std::uint32_t tangentMorphTargetCount;
            std::uint32_t skinAttributeCount;
            bool fragmentShaderGeneratedTBN;
            bool useTextureTransform;
            fastgltf::AlphaMode alphaMode;
            bool usePerFragmentEmissiveStencilExport;

            [[nodiscard]] std::strong_ordering operator<=>(const Config&) const noexcept = default;
        };

        PrimitiveRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::Primitive &pipelineLayout LIFETIMEBOUND,
            const rp::Scene &sceneRenderPass LIFETIMEBOUND,
            const Config &config
        );

    private:
        struct VertexShaderSpecialization;
        struct FragmentShaderSpecialization;

        [[nodiscard]] static std::array<int, 3> getVertexShaderVariants(const Config &config) noexcept;
        [[nodiscard]] static VertexShaderSpecialization getVertexShaderSpecialization(const Config &config) noexcept;
        [[nodiscard]] static std::array<int, 5> getFragmentShaderVariants(const Config &config) noexcept;
        [[nodiscard]] static FragmentShaderSpecialization getFragmentShaderSpecialization(const Config &config, vk::SampleCountFlagBits sampleCount) noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

struct vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderPipeline::VertexShaderSpecialization {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t normalComponentType;
    std::uint32_t tangentComponentType;
    std::uint32_t texcoord0ComponentType;
    std::uint32_t texcoord1ComponentType;
    std::uint32_t texcoord2ComponentType;
    std::uint32_t texcoord3ComponentType;
    vk::Bool32 texcoord0Normalized;
    vk::Bool32 texcoord1Normalized;
    vk::Bool32 texcoord2Normalized;
    vk::Bool32 texcoord3Normalized;
    std::uint32_t color0ComponentType;
    std::uint32_t color0ComponentCount;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t normalMorphTargetCount;
    std::uint32_t tangentMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

struct vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderPipeline::FragmentShaderSpecialization {
    vk::Bool32 useTextureTransform;
    vk::Bool32 useLodBasedAlphaCutoff;
};

vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderPipeline::PrimitiveRenderPipeline(
    const vk::raii::Device &device,
    const pl::Primitive &pipelineLayout,
    const rp::Scene &sceneRenderPass,
    const Config &config
) : Pipeline { [&] -> Pipeline {
    const auto vertexShaderSpecialization = getVertexShaderSpecialization(config);
    const vk::SpecializationInfo vertexShaderSpecializationInfo {
        SpecializationMap<VertexShaderSpecialization>::value,
        vk::ArrayProxyNoTemporaries<const VertexShaderSpecialization> { vertexShaderSpecialization },
    };

    const auto fragmentShaderSpecialization = getFragmentShaderSpecialization(config, sceneRenderPass.sampleCount);
    const vk::SpecializationInfo fragmentShaderSpecializationInfo {
        SpecializationMap<FragmentShaderSpecialization>::value,
        vk::ArrayProxyNoTemporaries<const FragmentShaderSpecialization> { fragmentShaderSpecialization },
    };

    const vk::raii::ShaderModule vertexShaderModule { device, vk::ShaderModuleCreateInfo {
        {},
        vku::lvalue(std::apply(LIFT(shader_selector::primitive_vert), getVertexShaderVariants(config))),
    } };
    const vk::raii::ShaderModule fragmentShaderModule { device, vk::ShaderModuleCreateInfo {
        {},
        vku::lvalue(std::apply(LIFT(shader_selector::primitive_frag), getFragmentShaderVariants(config))),
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

    vk::PipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo {
        {},
        true, true, vk::CompareOp::eGreater /* use reverse Z */, false,
        // Write stencil value as 1 if material is emissive.
        true,
        vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
        vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
    };

    vk::PipelineMultisampleStateCreateInfo multisampleStateCreateInfo { {}, sceneRenderPass.sampleCount };

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

    boost::container::static_vector<vk::DynamicState, 5> dynamicStates {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::ePrimitiveTopology,
    };
    if (!config.usePerFragmentEmissiveStencilExport) {
        dynamicStates.push_back(vk::DynamicState::eStencilReference);
    }
    vk::PipelineDynamicStateCreateInfo dynamicStateCreateInfo{};

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
        *pipelineLayout,
        *sceneRenderPass,
    };

    switch (config.alphaMode) {
        case fastgltf::AlphaMode::Opaque:
            createInfo.pColorBlendState = &opaqueColorBlendStateCreateInfo;

            dynamicStates.push_back(vk::DynamicState::eCullMode);
            dynamicStateCreateInfo.setDynamicStates(dynamicStates);

            createInfo.subpass = 0;
            break;
        case fastgltf::AlphaMode::Mask:
            multisampleStateCreateInfo.alphaToCoverageEnable = true;
            createInfo.pColorBlendState = &opaqueColorBlendStateCreateInfo;

            dynamicStates.push_back(vk::DynamicState::eCullMode);
            dynamicStateCreateInfo.setDynamicStates(dynamicStates);

            createInfo.subpass = 0;
            break;
        case fastgltf::AlphaMode::Blend:
            // Translucent objects' back faces shouldn't be culled.
            rasterizationStateCreateInfo.cullMode = vk::CullModeFlagBits::eNone;

            // Translucent objects shouldn't interfere with the pre-rendered depth buffer.
            depthStencilStateCreateInfo.depthWriteEnable = false;

            createInfo.pColorBlendState = &translucentColorBlendStateCreateInfo;
            dynamicStateCreateInfo.setDynamicStates(dynamicStates);
            createInfo.subpass = 1;
            break;
    }

    return { device, nullptr, createInfo };
}() } { }

std::array<int, 3> vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderPipeline::getVertexShaderVariants(const Config &config) noexcept {
    return {
        static_cast<int>(config.texcoordComponentTypeAndNormalized.size()),
        config.color0ComponentTypeAndCount.has_value(),
        config.fragmentShaderGeneratedTBN,
    };
}

vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderPipeline::VertexShaderSpecialization vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderPipeline::getVertexShaderSpecialization(const Config &config) noexcept {
    VertexShaderSpecialization result {
        .positionComponentType = getGLComponentType(config.positionComponentType),
        .positionNormalized = config.positionNormalized,
        .normalComponentType = config.normalComponentType.transform(fastgltf::getGLComponentType).value_or(0U),
        .tangentComponentType = config.tangentComponentType.transform(fastgltf::getGLComponentType).value_or(0U),
        .positionMorphTargetCount = config.positionMorphTargetCount,
        .normalMorphTargetCount = config.normalMorphTargetCount,
        .tangentMorphTargetCount = config.tangentMorphTargetCount,
        .skinAttributeCount = config.skinAttributeCount,
    };

    std::ranges::transform(config.texcoordComponentTypeAndNormalized | std::views::keys, &result.texcoord0ComponentType /* TODO: UB? */, fastgltf::getGLComponentType);
    std::ranges::copy(config.texcoordComponentTypeAndNormalized | std::views::values, &result.texcoord0Normalized /* TODO: UB? */);

    if (config.color0ComponentTypeAndCount) {
        result.color0ComponentType = getGLComponentType(config.color0ComponentTypeAndCount->first);
        result.color0ComponentCount = config.color0ComponentTypeAndCount->second;
    }

    return result;
}

std::array<int, 5> vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderPipeline::getFragmentShaderVariants(const Config &config) noexcept {
    return {
        static_cast<int>(config.texcoordComponentTypeAndNormalized.size()),
        config.color0ComponentTypeAndCount.has_value(),
        config.fragmentShaderGeneratedTBN,
        static_cast<int>(config.alphaMode),
        config.usePerFragmentEmissiveStencilExport,
    };
}

auto vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderPipeline::getFragmentShaderSpecialization(
    const Config &config,
    vk::SampleCountFlagBits sampleCount
) noexcept -> FragmentShaderSpecialization {
    return { config.useTextureTransform, sampleCount != vk::SampleCountFlagBits::e1 };
}