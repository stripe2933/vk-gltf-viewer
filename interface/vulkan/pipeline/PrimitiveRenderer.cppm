module;

#include <boost/container/static_vector.hpp>
#include <boost/container_hash/hash.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.PrimitiveRenderer;

import std;
export import fastgltf;

import vk_gltf_viewer.helpers.ranges;
import vk_gltf_viewer.shader_selector.primitive_frag;
import vk_gltf_viewer.shader_selector.primitive_vert;
export import vk_gltf_viewer.vulkan.pl.PrimitiveMultiview;
export import vk_gltf_viewer.vulkan.rp.Scene;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class PrimitiveRendererSpecialization {
    public:
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
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

        [[nodiscard]] bool operator==(const PrimitiveRendererSpecialization&) const noexcept = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::PrimitiveMultiview &pipelineLayout LIFETIMEBOUND,
            const rp::Scene &sceneRenderPass LIFETIMEBOUND
        ) const;

    private:
        struct VertexShaderSpecializationData;
        struct FragmentShaderSpecializationData;

        [[nodiscard]] std::array<int, 3> getVertexShaderVariants() const noexcept;
        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const noexcept;
        [[nodiscard]] std::array<int, 5> getFragmentShaderVariants() const noexcept;
        [[nodiscard]] FragmentShaderSpecializationData getFragmentShaderSpecializationData() const noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

struct vk_gltf_viewer::vulkan::pipeline::PrimitiveRendererSpecialization::VertexShaderSpecializationData {
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

struct vk_gltf_viewer::vulkan::pipeline::PrimitiveRendererSpecialization::FragmentShaderSpecializationData {
    vk::Bool32 useTextureTransform;
};

vk::raii::Pipeline vk_gltf_viewer::vulkan::pipeline::PrimitiveRendererSpecialization::createPipeline(
    const vk::raii::Device &device,
    const pl::PrimitiveMultiview &pipelineLayout,
    const rp::Scene &sceneRenderPass
) const {
    const auto vertexShaderSpecializationData = getVertexShaderSpecializationData();
    const vk::SpecializationInfo vertexShaderSpecializationInfo {
        SpecializationMap<VertexShaderSpecializationData>::value,
        vk::ArrayProxyNoTemporaries<const VertexShaderSpecializationData> { vertexShaderSpecializationData },
    };

    const auto fragmentShaderSpecializationData = getFragmentShaderSpecializationData();
    const vk::SpecializationInfo fragmentShaderSpecializationInfo {
        SpecializationMap<FragmentShaderSpecializationData>::value,
        vk::ArrayProxyNoTemporaries<const FragmentShaderSpecializationData> { fragmentShaderSpecializationData },
    };

    const vku::RefHolder pipelineStages = createPipelineStages(
        device,
        vku::Shader {
            std::apply(LIFT(shader_selector::primitive_vert), getVertexShaderVariants()),
            vk::ShaderStageFlagBits::eVertex,
            &vertexShaderSpecializationInfo,
        },
        vku::Shader {
            std::apply(LIFT(shader_selector::primitive_frag), getFragmentShaderVariants()),
            vk::ShaderStageFlagBits::eFragment,
            &fragmentShaderSpecializationInfo,
        });

    const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo {
        {},
        topologyClass.value_or(vk::PrimitiveTopology::eTriangleList),
    };

    switch (alphaMode) {
        case fastgltf::AlphaMode::Opaque:
            return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                .setPInputAssemblyState(&inputAssemblyStateCreateInfo)
                .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                    {},
                    true, true, vk::CompareOp::eGreater /* use reverse Z */, false,
                    // Write stencil value as 1 if material is emissive.
                    true,
                    vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                    vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                }))
                .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                    {},
                    vku::unsafeProxy([&]() {
                        boost::container::static_vector<vk::DynamicState, 5> result {
                            vk::DynamicState::eViewport,
                            vk::DynamicState::eScissor,
                            vk::DynamicState::ePrimitiveTopology,
                            vk::DynamicState::eCullMode,
                        };
                        if (!usePerFragmentEmissiveStencilExport) {
                            result.push_back(vk::DynamicState::eStencilReference);
                        }
                        return result;
                    }()),
                }))
                .setRenderPass(*sceneRenderPass)
                .setSubpass(0)
            };
        case fastgltf::AlphaMode::Mask:
            return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                .setPInputAssemblyState(&inputAssemblyStateCreateInfo)
                .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                    {},
                    true, true, vk::CompareOp::eGreater /* use reverse Z */, false,
                    // Write stencil value as 1 if material is emissive.
                    true,
                    vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                    vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                }))
                .setPMultisampleState(vku::unsafeAddress(vk::PipelineMultisampleStateCreateInfo {
                    {},
                    vk::SampleCountFlagBits::e4,
                    {}, {}, {},
                    true,
                }))
                .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                    {},
                    vku::unsafeProxy([&]() {
                        boost::container::static_vector<vk::DynamicState, 5> result {
                            vk::DynamicState::eViewport,
                            vk::DynamicState::eScissor,
                            vk::DynamicState::ePrimitiveTopology,
                            vk::DynamicState::eCullMode,
                        };
                        if (!usePerFragmentEmissiveStencilExport) {
                            result.push_back(vk::DynamicState::eStencilReference);
                        }
                        return result;
                    }()),
                }))
                .setRenderPass(*sceneRenderPass)
                .setSubpass(0)
            };
        case fastgltf::AlphaMode::Blend:
            return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
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
                    true, false, vk::CompareOp::eGreater, false,
                    // Write stencil value as 1 if material is emissive.
                    true,
                    vk::StencilOpState { {}, vk::StencilOp::eReplace, {}, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                    vk::StencilOpState { {}, vk::StencilOp::eReplace, {}, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
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
                    vku::unsafeProxy([&]() {
                        boost::container::static_vector<vk::DynamicState, 4> result {
                            vk::DynamicState::eViewport,
                            vk::DynamicState::eScissor,
                            vk::DynamicState::ePrimitiveTopology,
                        };
                        if (!usePerFragmentEmissiveStencilExport) {
                            result.push_back(vk::DynamicState::eStencilReference);
                        }
                        return result;
                    }()),
                }))
                .setRenderPass(*sceneRenderPass)
                .setSubpass(1)
            };
    }
    std::unreachable();
}

std::array<int, 3> vk_gltf_viewer::vulkan::pipeline::PrimitiveRendererSpecialization::getVertexShaderVariants() const noexcept {
    return {
        static_cast<int>(texcoordComponentTypeAndNormalized.size()),
        color0ComponentTypeAndCount.has_value(),
        fragmentShaderGeneratedTBN,
    };
}

vk_gltf_viewer::vulkan::pipeline::PrimitiveRendererSpecialization::VertexShaderSpecializationData vk_gltf_viewer::vulkan::pipeline::PrimitiveRendererSpecialization::getVertexShaderSpecializationData() const noexcept {
    VertexShaderSpecializationData result {
        .positionComponentType = getGLComponentType(positionComponentType),
        .positionNormalized = positionNormalized,
        .normalComponentType = normalComponentType.transform(fastgltf::getGLComponentType).value_or(0U),
        .tangentComponentType = tangentComponentType.transform(fastgltf::getGLComponentType).value_or(0U),
        .positionMorphTargetCount = positionMorphTargetCount,
        .normalMorphTargetCount = normalMorphTargetCount,
        .tangentMorphTargetCount = tangentMorphTargetCount,
        .skinAttributeCount = skinAttributeCount,
    };

    std::ranges::transform(texcoordComponentTypeAndNormalized | std::views::keys, &result.texcoord0ComponentType /* TODO: UB? */, fastgltf::getGLComponentType);
    std::ranges::copy(texcoordComponentTypeAndNormalized | std::views::values, &result.texcoord0Normalized /* TODO: UB? */);

    if (color0ComponentTypeAndCount) {
        result.color0ComponentType = getGLComponentType(color0ComponentTypeAndCount->first);
        result.color0ComponentCount = color0ComponentTypeAndCount->second;
    }

    return result;
}

std::array<int, 5> vk_gltf_viewer::vulkan::pipeline::PrimitiveRendererSpecialization::getFragmentShaderVariants() const noexcept {
    return {
        static_cast<int>(texcoordComponentTypeAndNormalized.size()),
        color0ComponentTypeAndCount.has_value(),
        fragmentShaderGeneratedTBN,
        static_cast<int>(alphaMode),
        usePerFragmentEmissiveStencilExport,
    };
}

vk_gltf_viewer::vulkan::pipeline::PrimitiveRendererSpecialization::FragmentShaderSpecializationData vk_gltf_viewer::vulkan::pipeline::PrimitiveRendererSpecialization::getFragmentShaderSpecializationData() const noexcept {
    return { useTextureTransform };
}