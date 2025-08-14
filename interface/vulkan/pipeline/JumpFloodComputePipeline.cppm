module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.JumpFloodComputePipeline;

import std;
export import vku;

import vk_gltf_viewer.math.extended_arithmetic;
import vk_gltf_viewer.shader.jump_flood_comp;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class JumpFloodComputePipeline {
    public:
        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit JumpFloodComputePipeline(const vk::raii::Device &device LIFETIMEBOUND);

        [[nodiscard]] bool compute(
            vk::CommandBuffer commandBuffer,
            vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
            std::uint32_t initialSampleOffset,
            const vk::Extent2D &imageExtent,
            std::uint32_t viewCount
        ) const;

    private:
        struct PushConstant;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

struct vk_gltf_viewer::vulkan::pipeline::JumpFloodComputePipeline::PushConstant {
    vk::Bool32 forward;
    std::uint32_t sampleOffset;
};

vk_gltf_viewer::vulkan::pipeline::JumpFloodComputePipeline::JumpFloodComputePipeline(const vk::raii::Device &device)
    : descriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(DescriptorSetLayout::getBindings({ 1, vk::ShaderStageFlagBits::eCompute })),
    } }
    , pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        vku::unsafeProxy(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eCompute,
            0, sizeof(PushConstant),
        }),
    } }
    , pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        createPipelineStages(
            device,
            vku::Shader { shader::jump_flood_comp, vk::ShaderStageFlagBits::eCompute }).get()[0],
        *pipelineLayout,
    } } { }

bool vk_gltf_viewer::vulkan::pipeline::JumpFloodComputePipeline::compute(
    vk::CommandBuffer commandBuffer,
    vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
    std::uint32_t initialSampleOffset,
    const vk::Extent2D &imageExtent,
    std::uint32_t viewCount
) const {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {});

    PushConstant pushConstant { .forward = true, .sampleOffset = initialSampleOffset };

    for (; pushConstant.sampleOffset > 0U; pushConstant.forward = !pushConstant.forward, pushConstant.sampleOffset >>= 1U) {
        commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
        commandBuffer.dispatch(
            math::divCeil(imageExtent.width, 16U),
            math::divCeil(imageExtent.height, 16U),
            viewCount);

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