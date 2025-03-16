module;

#include <cassert>

export module vk_gltf_viewer:vulkan.pipeline.UnlitPrimitiveRenderer;

import std;
export import fastgltf;
import vku;
import :helpers.ranges;
import :shader_selector.unlit_primitive_frag;
import :shader_selector.unlit_primitive_vert;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;
export import :vulkan.shader_type.TextureTransform;
import :vulkan.specialization_constants.SpecializationMap;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class UnlitPrimitiveRendererSpecialization {
    public:
        std::uint8_t positionComponentType;
        std::optional<std::uint8_t> baseColorTexcoordComponentType;
        std::optional<std::pair<std::uint8_t, std::uint8_t>> colorComponentCountAndType;
        std::uint32_t positionMorphTargetWeightCount = 0;
        std::uint32_t skinAttributeCount = 0;
        shader_type::TextureTransform baseColorTextureTransform = shader_type::TextureTransform::None;
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

            switch (alphaMode) {
                case fastgltf::AlphaMode::Opaque:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *layout, 1, true, vk::SampleCountFlagBits::e4)
                        .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                            {},
                            true, true, vk::CompareOp::eGreater, // Use reverse Z.
                        }))
                        .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                            {},
                            vku::unsafeProxy({
                                vk::DynamicState::eViewport,
                                vk::DynamicState::eScissor,
                                vk::DynamicState::eCullMode,
                            }),
                        }))
                        .setRenderPass(*sceneRenderPass)
                        .setSubpass(0)
                    };
                case fastgltf::AlphaMode::Mask:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *layout, 1, true, vk::SampleCountFlagBits::e4)
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
                                vk::DynamicState::eCullMode,
                            }),
                        }))
                        .setRenderPass(*sceneRenderPass)
                        .setSubpass(0)
                    };
                case fastgltf::AlphaMode::Blend:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *layout, 1, true, vk::SampleCountFlagBits::e4)
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
                        .setRenderPass(*sceneRenderPass)
                        .setSubpass(1)
                    };
            }
            std::unreachable();
        }

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t positionComponentType;
            std::uint32_t texcoordComponentType = 5126; // FLOAT
            std::uint32_t colorComponentCount = 0;
            std::uint32_t colorComponentType = 5126; // FLOAT
            std::uint32_t positionMorphTargetWeightCount;
            std::uint32_t skinAttributeCount;
        };

        struct FragmentShaderSpecializationData {
            std::uint32_t textureTransformType = 0x00000; // NONE
        };

        [[nodiscard]] std::array<int, 2> getVertexShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentType.has_value(),
                colorComponentCountAndType.has_value(),
            };
        }

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            VertexShaderSpecializationData result {
                .positionComponentType = positionComponentType,
                .positionMorphTargetWeightCount = positionMorphTargetWeightCount,
                .skinAttributeCount = skinAttributeCount,
            };

            if (baseColorTexcoordComponentType) {
                result.texcoordComponentType = *baseColorTexcoordComponentType;
            }

            if (colorComponentCountAndType) {
                assert(ranges::one_of(colorComponentCountAndType->first, 3, 4));
                assert(ranges::one_of(colorComponentCountAndType->second, 1 /* UNSIGNED_BYTE */, 3 /* UNSIGNED_SHORT */, 6 /* FLOAT */));
                result.colorComponentCount = colorComponentCountAndType->first;
                result.colorComponentType = colorComponentCountAndType->second;
            }

            return result;
        }

        [[nodiscard]] std::array<int, 3> getFragmentShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentType.has_value(),
                colorComponentCountAndType.has_value(),
                static_cast<int>(alphaMode),
            };
        }

        [[nodiscard]] FragmentShaderSpecializationData getFragmentShaderSpecializationData() const {
            FragmentShaderSpecializationData result{};
            if (baseColorTextureTransform != shader_type::TextureTransform::None) {
                result.textureTransformType = static_cast<std::uint32_t>(baseColorTextureTransform);
            }
            return result;
        }
    };
}