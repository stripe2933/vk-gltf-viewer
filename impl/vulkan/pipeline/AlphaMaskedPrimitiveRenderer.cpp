module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.AlphaMaskedPrimitiveRenderer;

import std;
import vku;

vk_gltf_viewer::vulkan::pipeline::AlphaMaskedPrimitiveRenderer::AlphaMaskedPrimitiveRenderer(
    const vk::raii::Device &device,
    vk::PipelineLayout primitiveRendererPipelineLayout
) : pipeline { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            vku::createPipelineStages(
                device,
                vku::Shader { COMPILED_SHADER_DIR "/alpha_masked_primitive.vert.spv", vk::ShaderStageFlagBits::eVertex },
                vku::Shader { COMPILED_SHADER_DIR "/alpha_masked_primitive.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
            primitiveRendererPipelineLayout,
            1, true,
            vk::SampleCountFlagBits::e4)
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
            vku::unsafeProxy({ vk::Format::eR16G16B16A16Sfloat }),
            vk::Format::eD32Sfloat,
        }
    }.get() } { }

auto vk_gltf_viewer::vulkan::pipeline::AlphaMaskedPrimitiveRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}