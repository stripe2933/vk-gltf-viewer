module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.SkyboxRenderer;

import std;

vk_gltf_viewer::vulkan::pipeline::SkyboxRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device,
    const vk::Sampler &sampler
) : vku::DescriptorSetLayouts<1> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &sampler },
            }),
        },
    } { }

vk_gltf_viewer::vulkan::pipeline::SkyboxRenderer::SkyboxRenderer(
    const Gpu &gpu,
    const buffer::CubeIndices &cubeIndices
) : sampler { gpu.device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, {},
        {}, {}, {},
        {},
        {}, {},
        {}, {},
        0.f, vk::LodClampNone,
    } },
    descriptorSetLayouts { gpu.device, *sampler },
    pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy(descriptorSetLayouts.getHandles()),
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eAllGraphics,
                0, sizeof(PushConstant),
            },
        }),
    } },
    pipeline { gpu.device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            vku::createPipelineStages(
                gpu.device,
                vku::Shader { COMPILED_SHADER_DIR "/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex },
                vku::Shader { COMPILED_SHADER_DIR "/skybox.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout,
            1, true,
            vk::SampleCountFlagBits::e4)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eNone, {},
                {}, {}, {}, {},
                1.f,
            }))
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, false, vk::CompareOp::eEqual,
            })),
        vk::PipelineRenderingCreateInfo {
            {},
            vku::unsafeProxy({ vk::Format::eR16G16B16A16Sfloat }),
            vk::Format::eD32Sfloat,
        },
    }.get() },
    cubeIndices { cubeIndices } { }

auto vk_gltf_viewer::vulkan::pipeline::SkyboxRenderer::draw(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
    commandBuffer.bindIndexBuffer(cubeIndices, 0, vk::IndexType::eUint16);
    commandBuffer.drawIndexed(36, 1, 0, 0, 0);
}