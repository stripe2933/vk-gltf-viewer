module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.pipeline.JumpFloodSeedRenderer;

import std;
import vku;
import :shader.jump_flood_seed_vert;
import :shader.jump_flood_seed_frag;
import :shader_selector.mask_jump_flood_seed_vert;
import :shader_selector.mask_jump_flood_seed_frag;
export import :vulkan.pl.PrimitiveNoShading;

#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
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

    [[nodiscard]] vk::raii::Pipeline createMaskJumpFloodSeedRenderer(
        const vk::raii::Device &device,
        const pl::PrimitiveNoShading &pipelineLayout,
        const std::optional<fastgltf::ComponentType> &baseColorTexcoordComponentType,
        bool hasColorAlphaAttribute
    ) {
        struct VertexShaderSpecializationData {
            std::uint32_t texcoordComponentType = 5126; // FLOAT
        } vertexShaderSpecializationData{};

        if (baseColorTexcoordComponentType) {
            vertexShaderSpecializationData.texcoordComponentType = getGLComponentType(*baseColorTexcoordComponentType);
        }

        return { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader {
                        shader_selector::mask_jump_flood_seed_vert(baseColorTexcoordComponentType.has_value(), hasColorAlphaAttribute),
                        vk::ShaderStageFlagBits::eVertex,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            vku::unsafeProxy(vk::SpecializationMapEntry { 0, offsetof(VertexShaderSpecializationData, texcoordComponentType), sizeof(VertexShaderSpecializationData::texcoordComponentType) }),
                            vk::ArrayProxyNoTemporaries<const VertexShaderSpecializationData> { vertexShaderSpecializationData },
                        }),
                    },
                    vku::Shader {
                        shader_selector::mask_jump_flood_seed_frag(baseColorTexcoordComponentType.has_value(), hasColorAlphaAttribute),
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
}