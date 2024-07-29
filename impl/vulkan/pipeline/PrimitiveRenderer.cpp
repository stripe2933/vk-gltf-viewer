module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.PrimitiveRenderer;

import std;
import vku;

vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device,
    const vk::Sampler &sampler,
    std::uint32_t textureCount
) : vku::DescriptorSetLayouts<3, 2, 2> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment },
                vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &sampler },
                vk::DescriptorSetLayoutBinding { 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &sampler },
            }),
        },
        vk::StructureChain {
            vk::DescriptorSetLayoutCreateInfo {
                vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                vku::unsafeProxy({
                    vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, 1 + textureCount, vk::ShaderStageFlagBits::eFragment },
                    vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics },
                }),
            },
            vk::DescriptorSetLayoutBindingFlagsCreateInfo {
                vku::unsafeProxy({
                    vk::Flags { vk::DescriptorBindingFlagBits::eUpdateAfterBind },
                    vk::DescriptorBindingFlags{},
                }),
            },
        }.get(),
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics },
                vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            }),
        },
    } { }

auto vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderer::DescriptorSets::getDescriptorWrites2(
    const vk::DescriptorBufferInfo &primitiveBufferInfo,
    const vk::DescriptorBufferInfo &nodeTransformBufferInfo
) const -> std::array<vk::WriteDescriptorSet, 2> {
    return {
        getDescriptorWrite<2, 0>().setBufferInfo(primitiveBufferInfo),
        getDescriptorWrite<2, 1>().setBufferInfo(nodeTransformBufferInfo),
    };
}

vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderer::PrimitiveRenderer(
    const vk::raii::Device &device,
    std::uint32_t textureCount
) : sampler { device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        {}, {}, {},
        {},
        false, {},
        {}, {},
        {}, vk::LodClampNone,
    } },
    descriptorSetLayouts { device, *sampler, textureCount },
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
                vku::Shader { COMPILED_SHADER_DIR "/primitive.vert.spv", vk::ShaderStageFlagBits::eVertex },
                vku::Shader { COMPILED_SHADER_DIR "/primitive.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout,
            1, true,
            vk::SampleCountFlagBits::e4)
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
            vku::unsafeProxy({ vk::Format::eR16G16B16A16Sfloat }),
            vk::Format::eD32Sfloat,
        }
    }.get() } { }

auto vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

auto vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderer::bindDescriptorSets(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t firstSet
) const -> void {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout,
        firstSet, std::span { descriptorSets }.subspan(firstSet), {});
}

auto vk_gltf_viewer::vulkan::pipeline::PrimitiveRenderer::pushConstants(
    vk::CommandBuffer commandBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
}