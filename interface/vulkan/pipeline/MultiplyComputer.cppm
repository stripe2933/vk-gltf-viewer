module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.MultiplyComputer;

import std;
import vku;
export import vulkan_hpp;
import :math.extended_arithmetic;
import :shader.multiply_comp;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class MultiplyComputer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer> {
            explicit DescriptorSetLayout(
                const vk::raii::Device &device LIFETIMEBOUND
            ) : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer> {
                    device,
                    vk::DescriptorSetLayoutCreateInfo {
                        vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                        vku::unsafeProxy(getBindings(
                            { 1, vk::ShaderStageFlagBits::eCompute },
                            { 1, vk::ShaderStageFlagBits::eCompute })),
                    }
                } { }
        };

        struct PushConstant {
            std::uint32_t numCount;
            float multiplier;
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit MultiplyComputer(
            const vk::raii::Device &device LIFETIMEBOUND
        ) : descriptorSetLayout { device },
            pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(PushConstant),
                }),
            } },
            pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
                {},
                createPipelineStages(
                    device,
                    vku::Shader { shader::multiply_comp, vk::ShaderStageFlagBits::eCompute }).get()[0],
                *pipelineLayout,
            } } { }

        auto compute(
            vk::CommandBuffer commandBuffer,
            vk::ArrayProxy<vk::WriteDescriptorSet> descriptorWrites,
            const PushConstant &pushConstant
        ) const -> void {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            commandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorWrites);
            commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
            commandBuffer.dispatch(math::divCeil(pushConstant.numCount, 256U), 1, 1);
        }
    };
}