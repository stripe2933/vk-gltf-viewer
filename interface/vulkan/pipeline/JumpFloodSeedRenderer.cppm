export module vk_gltf_viewer:vulkan.pipeline.JumpFloodSeedRenderer;

import std;
import vku;
import :shader.jump_flood_seed_vert;
import :shader.jump_flood_seed_frag;
import :shader.mask_jump_flood_seed_vert;
import :shader.mask_jump_flood_seed_frag;
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
        bool hasBaseColorTexture
    ) {
        return { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader {
                        hasBaseColorTexture
                            ? std::span<const std::uint32_t> { shader::mask_jump_flood_seed_vert<1> }
                            : std::span<const std::uint32_t> { shader::mask_jump_flood_seed_vert<0> },
                        vk::ShaderStageFlagBits::eVertex,
                    },
                    vku::Shader {
                        hasBaseColorTexture
                            ? std::span<const std::uint32_t> { shader::mask_jump_flood_seed_frag<1> }
                            : std::span<const std::uint32_t> { shader::mask_jump_flood_seed_frag<0> },
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