module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.BrdfmapComputer;

import std;
export import vku;
import :shader.brdfmap_comp;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct BrdfmapComputer {
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
            explicit DescriptorSetLayout(const vk::raii::Device &device LIFETIMEBOUND)
                : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
                    device,
                    vk::DescriptorSetLayoutCreateInfo {
                        vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                        vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eCompute })),
                    }
                } { }
        };

        struct SpecializationConstants {
            std::uint32_t numSamples;
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit BrdfmapComputer(const vk::raii::Device &device LIFETIMEBOUND, const SpecializationConstants &specializationConstants = { 1024 })
            : descriptorSetLayout { device }
            , pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                    {},
                    *descriptorSetLayout,
                } },
                pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
                    {},
                    createPipelineStages(
                        device,
                        vku::Shader {
                            shader::brdfmap_comp,
                            vk::ShaderStageFlagBits::eCompute,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                vku::unsafeProxy(vk::SpecializationMapEntry { 0, 0, sizeof(SpecializationConstants::numSamples) }),
                                vk::ArrayProxyNoTemporaries<const SpecializationConstants>(specializationConstants),
                            }),
                        }).get()[0],
                    *pipelineLayout,
                } } { }

        void compute(
            vk::CommandBuffer commandBuffer,
            vk::ArrayProxy<vk::WriteDescriptorSet> descriptorWrites,
            const vk::Extent2D &imageSize
        ) const {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            commandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorWrites);
            commandBuffer.dispatch(imageSize.width / 16, imageSize.height / 16, 1);
        }
    };
}