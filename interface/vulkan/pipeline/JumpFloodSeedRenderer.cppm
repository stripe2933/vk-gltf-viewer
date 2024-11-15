export module vk_gltf_viewer:vulkan.pipeline.JumpFloodSeedRenderer;

import vku;
export import :vulkan.pl.PrimitiveNoShading;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct JumpFloodSeedRenderer : vk::raii::Pipeline {
        JumpFloodSeedRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::PrimitiveNoShading &layout [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/jump_flood_seed.vert.spv", vk::ShaderStageFlagBits::eVertex),
                        vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/jump_flood_seed.frag.spv", vk::ShaderStageFlagBits::eFragment)).get(),
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