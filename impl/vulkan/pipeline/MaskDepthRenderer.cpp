module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.MaskDepthRenderer;

import std;

vk_gltf_viewer::vulkan::pipeline::MaskDepthRenderer::MaskDepthRenderer(
    const vk::raii::Device &device,
    std::tuple<const dsl::Scene&, const dsl::Asset&> descriptorSetLayouts
) : pipelineLayout { device, vk::PipelineLayoutCreateInfo{
        {},
        vku::unsafeProxy(std::apply([](const auto &...x) { return std::array { *x... }; }, descriptorSetLayouts)),
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eVertex,
                0, sizeof(PushConstant),
            },
        }),
    } },
    pipeline { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader { COMPILED_SHADER_DIR "/mask_depth.vert.spv", vk::ShaderStageFlagBits::eVertex },
                vku::Shader { COMPILED_SHADER_DIR "/mask_depth.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
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
            vku::unsafeProxy({ vk::Format::eR32Uint }),
            vk::Format::eD32Sfloat,
        }
    }.get() } { }

auto vk_gltf_viewer::vulkan::pipeline::MaskDepthRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

auto vk_gltf_viewer::vulkan::pipeline::MaskDepthRenderer::pushConstants(
    vk::CommandBuffer commandBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
}