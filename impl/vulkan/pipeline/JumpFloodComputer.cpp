module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.JumpFloodComputer;

import std;
import :helpers.extended_arithmetic;

struct vk_gltf_viewer::vulkan::pipeline::JumpFloodComputer::PushConstant {
    vk::Bool32 forward;
    std::uint32_t sampleOffset;
};

vk_gltf_viewer::vulkan::pipeline::JumpFloodComputer::DescriptorSetLayout::DescriptorSetLayout(
    const vk::raii::Device &device
) : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 2, vk::ShaderStageFlagBits::eCompute },
            }),
        },
    } { }

vk_gltf_viewer::vulkan::pipeline::JumpFloodComputer::JumpFloodComputer(
    const vk::raii::Device &device
) : descriptorSetLayout { device },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eCompute,
                0, sizeof(PushConstant),
            },
        }),
    } },
    pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        createPipelineStages(
            device,
            vku::Shader { COMPILED_SHADER_DIR "/jump_flood.comp.spv", vk::ShaderStageFlagBits::eCompute }).get()[0],
        *pipelineLayout,
    } } { }

auto vk_gltf_viewer::vulkan::pipeline::JumpFloodComputer::compute(
    vk::CommandBuffer commandBuffer,
    vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
    std::uint32_t initialSampleOffset,
    const vk::Extent2D &imageExtent
) const -> vk::Bool32 {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {});

    PushConstant pushConstant { .forward = true, .sampleOffset = initialSampleOffset };

    for (; pushConstant.sampleOffset > 0U; pushConstant.forward = !pushConstant.forward, pushConstant.sampleOffset >>= 1U) {
        commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
        commandBuffer.dispatch(
            divCeil(imageExtent.width, 16U),
            divCeil(imageExtent.height, 16U),
            1);

        if (pushConstant.sampleOffset != 1U) {
            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                {},
                vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                {}, {});
        }
    }
    return pushConstant.forward;
}