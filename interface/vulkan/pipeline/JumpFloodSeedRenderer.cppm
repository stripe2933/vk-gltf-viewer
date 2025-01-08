export module vk_gltf_viewer:vulkan.pipeline.JumpFloodSeedRenderer;

import std;
import vku;
import :shader.jump_flood_seed_vert;
import :shader.jump_flood_seed_frag;
import :shader_selector.mask_jump_flood_seed_vert;
import :shader_selector.mask_jump_flood_seed_frag;
export import :vulkan.pl.PrimitiveNoShading;
import :vulkan.specialization_constants.SpecializationMap;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
    class MaskJumpFloodSeedRendererSpecialization {
    public:
        std::optional<fastgltf::ComponentType> baseColorTexcoordComponentType;
        std::optional<fastgltf::ComponentType> colorAlphaComponentType;

        [[nodiscard]] bool operator==(const MaskJumpFloodSeedRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device,
            const pl::PrimitiveNoShading &pipelineLayout
        ) const {
            return { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader {
                            std::apply(LIFT(shader_selector::mask_jump_flood_seed_vert), getVertexShaderVariants()),
                            vk::ShaderStageFlagBits::eVertex,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                SpecializationMap<VertexShaderSpecializationData>::value,
                                vku::unsafeProxy(getVertexShaderSpecializationData()),
                            }),
                        },
                        vku::Shader {
                            std::apply(LIFT(shader_selector::mask_jump_flood_seed_frag), getFragmentShaderVariants()),
                            vk::ShaderStageFlagBits::eFragment,
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
                    vku::unsafeProxy(vk::Format::eR16G16Uint),
                    vk::Format::eD32Sfloat,
                }
            }.get() };
        }

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t texcoordComponentType = 5126; // FLOAT
            std::uint32_t colorComponentType = 5126; // FLOAT
        };

        [[nodiscard]] std::array<int, 2> getVertexShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentType.has_value(),
                colorAlphaComponentType.has_value(),
            };
        }

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            VertexShaderSpecializationData result{};

            if (baseColorTexcoordComponentType) {
                result.texcoordComponentType = getGLComponentType(*baseColorTexcoordComponentType);
            }
            if (colorAlphaComponentType) {
                result.colorComponentType = getGLComponentType(*colorAlphaComponentType);
            }

            return result;
        }

        [[nodiscard]] std::array<int, 2> getFragmentShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentType.has_value(),
                colorAlphaComponentType.has_value(),
            };
        }
    };
    
    [[nodiscard]] vk::raii::Pipeline createJumpFloodSeedRenderer(
        const vk::raii::Device &device,
        const pl::PrimitiveNoShading &pipelineLayout
    ) {
        return { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::jump_flood_seed_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::jump_flood_seed_frag, vk::ShaderStageFlagBits::eFragment }).get(),
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
                vku::unsafeProxy(vk::Format::eR16G16Uint),
                vk::Format::eD32Sfloat,
            }
        }.get() };
    }
}