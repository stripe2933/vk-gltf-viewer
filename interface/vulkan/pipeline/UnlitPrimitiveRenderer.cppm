module;

#include <cassert>

export module vk_gltf_viewer:vulkan.pipeline.UnlitPrimitiveRenderer;

import std;
export import fastgltf;
import vku;
import :shader_selector.unlit_primitive_frag;
import :shader_selector.unlit_primitive_vert;

import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.vulkan.pl.Primitive;
export import vk_gltf_viewer.vulkan.rp.Scene;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class UnlitPrimitiveRendererSpecialization {
    public:
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::optional<std::pair<fastgltf::ComponentType, bool>> baseColorTexcoordComponentTypeAndNormalized;
        std::optional<std::pair<fastgltf::ComponentType, std::uint8_t>> color0ComponentTypeAndCount;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;
        bool useTextureTransform;
        fastgltf::AlphaMode alphaMode;

        [[nodiscard]] bool operator==(const UnlitPrimitiveRendererSpecialization&) const noexcept = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device,
            const pl::Primitive &layout,
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
                    std::apply(LIFT(shader_selector::unlit_primitive_vert), getVertexShaderVariants()),
                    vk::ShaderStageFlagBits::eVertex,
                    &vertexShaderSpecializationInfo
                },
                vku::Shader {
                    std::apply(LIFT(shader_selector::unlit_primitive_frag), getFragmentShaderVariants()),
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
        }

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t positionComponentType;
            vk::Bool32 positionNormalized;
            std::uint32_t baseColorTexcoordComponentType;
            vk::Bool32 baseColorTexcoordNormalized;
            std::uint32_t color0ComponentType;
            std::uint32_t color0ComponentCount;
            std::uint32_t positionMorphTargetCount;
            std::uint32_t skinAttributeCount;
        };

        struct FragmentShaderSpecializationData {
            vk::Bool32 useTextureTransform;
        };

        [[nodiscard]] std::array<int, 2> getVertexShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentTypeAndNormalized.has_value(),
                color0ComponentTypeAndCount.has_value(),
            };
        }

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            VertexShaderSpecializationData result {
                .positionComponentType = getGLComponentType(positionComponentType),
                .positionNormalized = positionNormalized,
                .positionMorphTargetCount = positionMorphTargetCount,
                .skinAttributeCount = skinAttributeCount,
            };

            if (baseColorTexcoordComponentTypeAndNormalized) {
                result.baseColorTexcoordComponentType = getGLComponentType(baseColorTexcoordComponentTypeAndNormalized->first);
                result.baseColorTexcoordNormalized = baseColorTexcoordComponentTypeAndNormalized->second;
            }

            if (color0ComponentTypeAndCount) {
                result.color0ComponentType = getGLComponentType(color0ComponentTypeAndCount->first);
                result.color0ComponentCount = color0ComponentTypeAndCount->second;
            }

            return result;
        }

        [[nodiscard]] std::array<int, 3> getFragmentShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentTypeAndNormalized.has_value(),
                color0ComponentTypeAndCount.has_value(),
                static_cast<int>(alphaMode),
            };
        }

        [[nodiscard]] FragmentShaderSpecializationData getFragmentShaderSpecializationData() const {
            return { useTextureTransform };
        }
    };
}