export module vk_gltf_viewer:vulkan.pipeline.AlphaMaskedPrimitiveRenderer;

import vku;
export import :vulkan.pl.SceneRendering;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct AlphaMaskedPrimitiveRenderer : vk::raii::Pipeline {
        AlphaMaskedPrimitiveRenderer(
        const vk::raii::Device &device [[clang::lifetimebound]],
        const pl::SceneRendering &layout [[clang::lifetimebound]]
    ) : Pipeline { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { COMPILED_SHADER_DIR "/alpha_masked_primitive.vert.spv", vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { COMPILED_SHADER_DIR "/alpha_masked_primitive.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
                *layout, 1, true, vk::SampleCountFlagBits::e4)
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
                })),
            vk::PipelineRenderingCreateInfo {
                {},
                vku::unsafeProxy({ vk::Format::eB8G8R8A8Srgb }),
                vk::Format::eD32Sfloat,
            }
        }.get() } { }
    };
}