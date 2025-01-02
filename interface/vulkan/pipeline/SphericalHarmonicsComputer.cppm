module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pipeline.SphericalHarmonicsComputer;

import std;
import vku;
export import vulkan_hpp;
import :shader.spherical_harmonics_comp;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    class SphericalHarmonicsComputer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage, vk::DescriptorType::eStorageBuffer> {
            explicit DescriptorSetLayout(
                const vk::raii::Device &device [[clang::lifetimebound]]
            ) : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage, vk::DescriptorType::eStorageBuffer> {
                device,
                    vk::DescriptorSetLayoutCreateInfo {
                        {},
                        vku::unsafeProxy(getBindings(
                            { 1, vk::ShaderStageFlagBits::eCompute },
                            { 1, vk::ShaderStageFlagBits::eCompute })),
                    },
                } { }
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit SphericalHarmonicsComputer(
            const vk::raii::Device &device [[clang::lifetimebound]]
        ) : descriptorSetLayout { device },
            pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
            } },
            pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
                {},
                createPipelineStages(
                    device,
                    vku::Shader { shader::spherical_harmonics_comp, vk::ShaderStageFlagBits::eCompute }).get()[0],
                *pipelineLayout,
            } } { }

        auto compute(
            vk::CommandBuffer commandBuffer,
            vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
            std::uint32_t cubemapSize
        ) const -> void  {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {});
            const std::array workgroupCount = getWorkgroupCount(cubemapSize);
            commandBuffer.dispatch(workgroupCount[0], workgroupCount[1], workgroupCount[2]);
        }

        [[nodiscard]] static auto getWorkgroupCount(
            std::uint32_t cubemapSize
        ) noexcept -> std::array<std::uint32_t, 3> {
            return { cubemapSize / 16U, cubemapSize / 16U, 1 };
        }
    };
}