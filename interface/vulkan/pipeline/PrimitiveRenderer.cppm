export module vk_gltf_viewer:vulkan.pipeline.PrimitiveRenderer;

import vku;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct PrimitiveRenderer : vk::raii::Pipeline {
        PrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::Primitive &layout [[clang::lifetimebound]],
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/primitive.vert.spv", vk::ShaderStageFlagBits::eVertex),
                    vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/primitive.frag.spv", vk::ShaderStageFlagBits::eFragment)).get(),
                *layout, 1, true, vk::SampleCountFlagBits::e4)
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
        } { }
    };
}