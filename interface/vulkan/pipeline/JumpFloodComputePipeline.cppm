module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.JumpFloodComputePipeline;

import std;
export import vku;

import vk_gltf_viewer.shader.jump_flood_comp;

namespace {
    struct PushConstant {
        vk::Bool32 forward;
        std::uint32_t sampleOffset;
    };
}

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class JumpFloodComputePipeline {
    public:
        using DescriptorSetLayout = vku::raii::DescriptorSetLayout<vk::DescriptorType::eStorageImage>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit JumpFloodComputePipeline(const vk::raii::Device &device LIFETIMEBOUND)
            : descriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::lvalue(DescriptorSetLayout::getCreateInfoBinding<0>(1, vk::ShaderStageFlagBits::eCompute)),
            } }
            , pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::lvalue(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(PushConstant),
                }),
            } }
            , pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
                {},
                vk::PipelineShaderStageCreateInfo {
                    {},
                    vk::ShaderStageFlagBits::eCompute,
                    *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                        {},
                        shader::jump_flood_comp,
                    } }),
                    "main",
                },
                *pipelineLayout,
            } } { }

        [[nodiscard]] bool compute(
            vk::CommandBuffer commandBuffer,
            vk::DescriptorSet descriptorSet,
            std::uint32_t initialSampleOffset,
            const vk::Extent2D &imageExtent,
            std::uint32_t viewCount
        ) const {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline, *pipeline.getDispatcher());
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {}, *pipeline.getDispatcher());

            PushConstant pushConstant { .forward = true, .sampleOffset = initialSampleOffset };

            for (; pushConstant.sampleOffset > 0U; pushConstant.forward = !pushConstant.forward, pushConstant.sampleOffset >>= 1U) {
                commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant, *pipeline.getDispatcher());
                commandBuffer.dispatch(
                    vku::divCeil(imageExtent.width, 16U),
                    vku::divCeil(imageExtent.height, 16U),
                    viewCount,
                    *pipeline.getDispatcher());

                if (pushConstant.sampleOffset != 1U) {
                    commandBuffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                        {},
                        vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                        {}, {},
                        *pipeline.getDispatcher());
                }
            }
            return pushConstant.forward;
        }
    };
}