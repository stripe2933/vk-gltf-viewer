module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.OutlineRenderer;

import std;

vk_gltf_viewer::vulkan::pipeline::OutlineRenderer::DescriptorSetLayout::DescriptorSetLayout(
    const vk::raii::Device &device
) : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eFragment },
            }),
        },
    } { }

vk_gltf_viewer::vulkan::pipeline::OutlineRenderer::OutlineRenderer(
    const vk::raii::Device &device
) : descriptorSetLayout { device },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo{
        {},
        *descriptorSetLayout,
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eFragment,
                0, sizeof(PushConstant),
            },
        }),
    } },
    pipeline { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader { COMPILED_SHADER_DIR "/outline.vert.spv", vk::ShaderStageFlagBits::eVertex },
                vku::Shader { COMPILED_SHADER_DIR "/outline.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout,
            1)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eNone, {},
                {}, {}, {}, {},
                1.0f,
            }))
            .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                {},
                false, {},
                vku::unsafeProxy({
                    vk::PipelineColorBlendAttachmentState {
                        true,
                        vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                        vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                    },
                }),
            })),
        vk::PipelineRenderingCreateInfo {
            {},
            vku::unsafeProxy({ vk::Format::eB8G8R8A8Srgb }),
        },
    }.get() } { }

auto vk_gltf_viewer::vulkan::pipeline::OutlineRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

auto vk_gltf_viewer::vulkan::pipeline::OutlineRenderer::bindDescriptorSets(
    vk::CommandBuffer commandBuffer,
    vku::DescriptorSet<DescriptorSetLayout> descriptorSet
) const -> void {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSet, {});
}

auto vk_gltf_viewer::vulkan::pipeline::OutlineRenderer::pushConstants(
    vk::CommandBuffer commandBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, pushConstant);
}

auto vk_gltf_viewer::vulkan::pipeline::OutlineRenderer::draw(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.draw(3, 1, 0, 0);
}