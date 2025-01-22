module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pipeline.CubemapComputer;

import std;
import vku;
export import vulkan_hpp;
import :shader.cubemap_comp;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct CubemapComputer {
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> {
            DescriptorSetLayout(
                const vk::raii::Device &device [[clang::lifetimebound]],
                const vk::Sampler &sampler [[clang::lifetimebound]]
            ) : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> {
                    device,
                    vk::DescriptorSetLayoutCreateInfo {
                        vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                        vku::unsafeProxy(getBindings(
                            { 1, vk::ShaderStageFlagBits::eCompute, &sampler },
                            { 1, vk::ShaderStageFlagBits::eCompute })),
                    },
                } { }
        };

        vk::raii::Sampler eqmapSampler;
        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit CubemapComputer(
            const vk::raii::Device &device [[clang::lifetimebound]]
        ) : eqmapSampler { device, vk::SamplerCreateInfo {
                {},
                vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
                {}, {}, {},
                {},
                false, {},
                {}, {},
                0.f, vk::LodClampNone,
            } },
            descriptorSetLayout { device, *eqmapSampler },
            pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
            } },
            pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
                {},
                createPipelineStages(
                    device,
                    vku::Shader { shader::cubemap_comp, vk::ShaderStageFlagBits::eCompute }).get()[0],
                *pipelineLayout,
            } } { }

        void compute(
            vk::CommandBuffer commandBuffer,
            vk::ArrayProxy<vk::WriteDescriptorSet> descriptorWrites,
            std::uint32_t cubemapSize
        ) const {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            commandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorWrites);
            commandBuffer.dispatch(cubemapSize / 16, cubemapSize / 16, 6);
        }
    };
}