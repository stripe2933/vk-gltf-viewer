export module vk_gltf_viewer:vulkan.pipeline.JumpFloodSeedRenderer;

import std;
import vku;
import :shader.jump_flood_seed_vert;
import :shader.jump_flood_seed_frag;
import :shader_selector.mask_jump_flood_seed_vert;
import :shader_selector.mask_jump_flood_seed_frag;
export import :vulkan.pl.PrimitiveNoShading;

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
        bool hasBaseColorTexture,
        bool hasColorAlphaAttribute
    ) {
        return { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader {
                        shader_selector::mask_jump_flood_seed_vert(hasBaseColorTexture, hasColorAlphaAttribute),
                        vk::ShaderStageFlagBits::eVertex,
                    },
                    vku::Shader {
                        shader_selector::mask_jump_flood_seed_frag(hasBaseColorTexture, hasColorAlphaAttribute),
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