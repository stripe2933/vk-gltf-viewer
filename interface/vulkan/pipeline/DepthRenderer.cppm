export module vk_gltf_viewer:vulkan.pipeline.DepthRenderer;

import std;
import vku;
import :shader.depth_vert;
import :shader.depth_frag;
import :shader_selector.mask_depth_vert;
import :shader_selector.mask_depth_frag;
export import :vulkan.pl.PrimitiveNoShading;
import :vulkan.shader_type.TextureTransform;
import :vulkan.specialization_constants.SpecializationMap;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class DepthRendererSpecialization {
    public:
        std::uint32_t positionMorphTargetWeightCount = 0;

        [[nodiscard]] bool operator==(const DepthRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device,
            const pl::PrimitiveNoShading &pipelineLayout
        ) const {
            return { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader {
                            shader::depth_vert,
                            vk::ShaderStageFlagBits::eVertex,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                SpecializationMap<VertexShaderSpecializationData>::value,
                                vku::unsafeProxy(getVertexShaderSpecializationData()),
                            }),
                        },
                        vku::Shader { shader::depth_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                    *pipelineLayout, 1, true)
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
                    })),
                vk::PipelineRenderingCreateInfo {
                    {},
                    vku::unsafeProxy(vk::Format::eR16Uint),
                    vk::Format::eD32Sfloat,
                }
            }.get() };
        }

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t positionMorphTargetWeightCount;
        };

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            return { positionMorphTargetWeightCount };
        }
    };

    class MaskDepthRendererSpecialization {
    public:
        std::optional<std::uint8_t> baseColorTexcoordComponentType;
        std::optional<std::uint8_t> colorAlphaComponentType;
        std::uint32_t positionMorphTargetWeightCount = 0;
        shader_type::TextureTransform baseColorTextureTransform = shader_type::TextureTransform::None;

        [[nodiscard]] bool operator==(const MaskDepthRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device,
            const pl::PrimitiveNoShading &pipelineLayout
        ) const {
            return { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader {
                            std::apply(LIFT(shader_selector::mask_depth_vert), getVertexShaderVariants()),
                            vk::ShaderStageFlagBits::eVertex,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                SpecializationMap<VertexShaderSpecializationData>::value,
                                vku::unsafeProxy(getVertexShaderSpecializationData()),
                            }),
                        },
                        vku::Shader {
                            std::apply(LIFT(shader_selector::mask_depth_frag), getFragmentShaderVariants()),
                            vk::ShaderStageFlagBits::eFragment,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                SpecializationMap<FragmentShaderSpecializationData>::value,
                                vku::unsafeProxy(getFragmentShaderSpecializationData()),
                            }),
                        }).get(),
                    *pipelineLayout, 1, true)
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
                    })),
                vk::PipelineRenderingCreateInfo {
                    {},
                    vku::unsafeProxy(vk::Format::eR16Uint),
                    vk::Format::eD32Sfloat,
                }
            }.get() };
        }

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t texcoordComponentType = 5126; // FLOAT
            std::uint32_t colorComponentType = 5126; // FLOAT
            std::uint32_t positionMorphTargetWeightCount;
        };

        struct FragmentShaderSpecializationData {
            std::uint32_t textureTransformType = 0x00000; // NONE
        };

        [[nodiscard]] std::array<int, 2> getVertexShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentType.has_value(),
                colorAlphaComponentType.has_value(),
            };
        }

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            VertexShaderSpecializationData result {
                .positionMorphTargetWeightCount = positionMorphTargetWeightCount,
            };

            if (baseColorTexcoordComponentType) {
                result.texcoordComponentType = *baseColorTexcoordComponentType;
            }
            if (colorAlphaComponentType) {
                result.colorComponentType = *colorAlphaComponentType;
            }

            return result;
        }

        [[nodiscard]] std::array<int, 2> getFragmentShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentType.has_value(),
                colorAlphaComponentType.has_value(),
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