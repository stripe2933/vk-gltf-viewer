module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.PrefilteredmapComputer;

import std;
import :math.extended_arithmetic;
import :shader.prefilteredmap_comp;
export import :vulkan.Gpu;
import :vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class PrefilteredmapComputer {
        struct PushConstant {
            std::int32_t mipLevel;
        };

    public:
        struct SpecializationConstants {
            std::uint32_t roughnessLevels;
            std::uint32_t samples;
        };

        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> {
            DescriptorSetLayout(
                const Gpu &gpu LIFETIMEBOUND,
                const vk::Sampler &sampler LIFETIMEBOUND,
                std::uint32_t roughnessLevels
            ) : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> {
                gpu.device,
                vk::DescriptorSetLayoutCreateInfo {
                    vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                    vku::unsafeProxy(getBindings(
                        { 1, vk::ShaderStageFlagBits::eCompute, &sampler },
                        { gpu.supportShaderImageLoadStoreLod ? 1U : roughnessLevels, vk::ShaderStageFlagBits::eCompute })),
                },
            } { }
        };

        vk::raii::Sampler cubemapSampler;
        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        std::uint32_t roughnessLevels;
        vk::raii::Pipeline pipeline;

        PrefilteredmapComputer(
            const Gpu &gpu LIFETIMEBOUND,
            const SpecializationConstants &specializationConstants
        ) : cubemapSampler { gpu.device, vk::SamplerCreateInfo {
                {},
                vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest,
                {}, {}, {},
                {},
                false, {},
                {}, {},
                0.f, vk::LodClampNone,
            } },
            descriptorSetLayout { gpu, *cubemapSampler, specializationConstants.roughnessLevels },
            pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(PushConstant),
                }),
            } },
            roughnessLevels { specializationConstants.roughnessLevels },
            pipeline { gpu.device, nullptr, vk::ComputePipelineCreateInfo {
                {},
                createPipelineStages(
                    gpu.device,
                    vku::Shader {
                        gpu.supportShaderImageLoadStoreLod
                            ? std::span<const std::uint32_t> { shader::prefilteredmap_comp<1> }
                            : std::span<const std::uint32_t> { shader::prefilteredmap_comp<0> },
                        vk::ShaderStageFlagBits::eCompute,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            SpecializationMap<SpecializationConstants>::value,
                            vk::ArrayProxyNoTemporaries<const SpecializationConstants>(specializationConstants),
                        }),
                    }).get()[0],
                *pipelineLayout,
            } } { }

        void compute(
            vk::CommandBuffer commandBuffer,
            vk::ArrayProxy<vk::WriteDescriptorSet> descriptorWrites,
            std::uint32_t prefilteredmapSize
        ) const {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            commandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorWrites);
            for (std::uint32_t mipLevel = 0; mipLevel < roughnessLevels; ++mipLevel) {
                commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, PushConstant { static_cast<std::int32_t>(mipLevel) });
                commandBuffer.dispatch(
                    math::divCeil(prefilteredmapSize >> mipLevel, 16U),
                    math::divCeil(prefilteredmapSize >> mipLevel, 16U),
                    6);
            }
        }
    };
}