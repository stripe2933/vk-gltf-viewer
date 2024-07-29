module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.SphericalHarmonicsRenderer;

import std;
import vku;

vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment },
            }),
        },
    } { }

auto vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::DescriptorSets::getDescriptorWrites0(
    const vk::DescriptorBufferInfo &cubemapSphericalHarmonicsBufferInfo
) const -> std::array<vk::WriteDescriptorSet, 1> {
    return {
        getDescriptorWrite<0, 0>().setBufferInfo(cubemapSphericalHarmonicsBufferInfo),
    };
}

vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::SphericalHarmonicsRenderer(
    const Gpu &gpu
) : descriptorSetLayouts { gpu.device },
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
                vku::Shader { COMPILED_SHADER_DIR "/spherical_harmonics.vert.spv", vk::ShaderStageFlagBits::eVertex },
                vku::Shader { COMPILED_SHADER_DIR "/spherical_harmonics.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
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
    indexBuffer {
        gpu.allocator,
        std::from_range, std::vector<std::uint16_t> {
            2, 6, 7, 2, 3, 7, 0, 4, 5, 0, 1, 5, 0, 2, 6, 0, 4, 6,
            1, 3, 7, 1, 5, 7, 0, 2, 3, 0, 1, 3, 4, 6, 7, 4, 5, 7,
        },
        vk::BufferUsageFlagBits::eIndexBuffer,
    } { }

auto vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::draw(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
    commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint16);
    commandBuffer.drawIndexed(36, 1, 0, 0, 0);
}