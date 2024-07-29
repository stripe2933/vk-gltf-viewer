module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.DepthRenderer;

import std;
import vku;

vk_gltf_viewer::vulkan::pipelines::DepthRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<2> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
                vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            }),
        },
    } { }

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::DescriptorSets::getDescriptorWrites0(
    const vk::DescriptorBufferInfo &primitiveBufferInfo,
    const vk::DescriptorBufferInfo &nodeTransformBufferInfo
) const -> std::array<vk::WriteDescriptorSet, 2> {
    return {
        getDescriptorWrite<0, 0>().setBufferInfo(primitiveBufferInfo),
        getDescriptorWrite<0, 1>().setBufferInfo(nodeTransformBufferInfo),
    };
}

vk_gltf_viewer::vulkan::pipelines::DepthRenderer::DepthRenderer(
    const vk::raii::Device &device
) : descriptorSetLayouts { device },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo{
        {},
        vku::unsafeProxy(descriptorSetLayouts.getHandles()),
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eAllGraphics,
                0, sizeof(PushConstant),
            },
        }),
    } },
    pipeline { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            vku::createPipelineStages(
                device,
                vku::Shader { COMPILED_SHADER_DIR "/depth.vert.spv", vk::ShaderStageFlagBits::eVertex },
                vku::Shader { COMPILED_SHADER_DIR "/depth.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout,
            3, true)
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
            vku::unsafeProxy({ vk::Format::eR32Uint, vk::Format::eR16G16Uint, vk::Format::eR16G16Uint }),
            vk::Format::eD32Sfloat,
        }
    }.get() } { }

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::bindDescriptorSets(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t firstSet
) const -> void {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, firstSet, descriptorSets, {});
}

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::pushConstants(
    vk::CommandBuffer commandBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
}