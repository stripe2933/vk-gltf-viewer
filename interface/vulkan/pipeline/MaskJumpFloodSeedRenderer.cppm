export module vk_gltf_viewer:vulkan.pipeline.MaskJumpFloodSeedRenderer;

import vku;
import :shader.mask_jump_flood_seed_vert;
import :shader.mask_jump_flood_seed_frag;
export import :vulkan.pl.PrimitiveNoShading;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct MaskJumpFloodSeedRenderer : vk::raii::Pipeline {
        MaskJumpFloodSeedRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::PrimitiveNoShading &layout [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader { shader::mask_jump_flood_seed_vert, vk::ShaderStageFlagBits::eVertex },
                        vku::Shader { shader::mask_jump_flood_seed_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                    *layout, 1, true)
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
            }.get() } { }
    };
}