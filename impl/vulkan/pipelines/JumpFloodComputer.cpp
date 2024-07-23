module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.JumpFloodComputer;

import std;
import vku;

template <std::unsigned_integral T>
[[nodiscard]] constexpr auto divCeil(T num, T denom) noexcept -> T {
    return (num / denom) + (num % denom != 0);
}

vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 2, vk::ShaderStageFlagBits::eCompute },
            }),
        },
    } { }

vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::JumpFloodComputer(
    const vk::raii::Device &device
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device) } { }

auto vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t initialSampleOffset,
    const vk::Extent2D &imageExtent
) const -> vk::Bool32 {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});

    PushConstant pushConstant {
        .forward = true,
        .sampleOffset = initialSampleOffset,
    };
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

auto vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::createPipelineLayout(
    const vk::raii::Device &device
) const -> decltype(pipelineLayout) {
    return { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy(descriptorSetLayouts.getHandles()),
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eCompute,
                0, sizeof(PushConstant),
            },
        }),
    } };
}

auto vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::createPipeline(
    const vk::raii::Device &device
) const -> decltype(pipeline) {
    return { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(vku::createPipelineStages(
            device,
            vku::Shader { COMPILED_SHADER_DIR "/jump_flood.comp.spv", vk::ShaderStageFlagBits::eCompute }).get()),
        *pipelineLayout,
    } };
}