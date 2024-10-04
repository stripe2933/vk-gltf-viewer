module;

#include <cstddef>

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pipeline.PrefilteredmapComputer;

import std;
import vku;
export import vulkan_hpp;
import :helpers.extended_arithmetic;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class PrefilteredmapComputer {
        struct PushConstant {
            std::uint32_t mipLevel;
        };

    public:
        struct SpecializationConstants {
            std::uint32_t roughnessLevels;
            std::uint32_t samples;
        };

        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> {
            DescriptorSetLayout(
                const vk::raii::Device &device [[clang::lifetimebound]],
                const vk::Sampler &sampler [[clang::lifetimebound]],
                std::uint32_t roughnessLevels
            )  : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> {
                    device,
                    vk::StructureChain {
                        vk::DescriptorSetLayoutCreateInfo {
                            vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                            vku::unsafeProxy({
                                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute, &sampler },
                                vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageImage, roughnessLevels, vk::ShaderStageFlagBits::eCompute },
                            }),
                        },
                        vk::DescriptorSetLayoutBindingFlagsCreateInfo {
                            vku::unsafeProxy({
                                vk::DescriptorBindingFlags{},
                                vk::Flags { vk::DescriptorBindingFlagBits::eUpdateAfterBind },
                            }),
                        },
                    }.get(),
                } { }
        };

        vk::raii::Sampler cubemapSampler;
        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        std::uint32_t roughnessLevels;
        vk::raii::Pipeline pipeline;

        PrefilteredmapComputer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const SpecializationConstants &specializationConstants
        ) : cubemapSampler { device, vk::SamplerCreateInfo {
                {},
                vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest,
                {}, {}, {},
                {},
                false, {},
                {}, {},
                0.f, vk::LodClampNone,
            } },
            descriptorSetLayout { device, *cubemapSampler, specializationConstants.roughnessLevels },
            pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(PushConstant),
                }),
            } },
            roughnessLevels { specializationConstants.roughnessLevels },
            pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
                {},
                createPipelineStages(
                    device,
                    vku::Shader {
                        COMPILED_SHADER_DIR "/prefilteredmap.comp.spv",
                        vk::ShaderStageFlagBits::eCompute,
                        vk::SpecializationInfo {
                            vku::unsafeProxy({
                                vk::SpecializationMapEntry { 0, offsetof(SpecializationConstants, roughnessLevels), sizeof(SpecializationConstants::roughnessLevels) },
                                vk::SpecializationMapEntry { 0, offsetof(SpecializationConstants, samples), sizeof(SpecializationConstants::samples) },
                            }),
                            vk::ArrayProxyNoTemporaries(specializationConstants),
                        },
                    }).get()[0],
                *pipelineLayout,
            } } { }

        auto compute(
            vk::CommandBuffer commandBuffer,
            vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
            std::uint32_t prefilteredmapSize
        ) const -> void {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {});
            for (std::uint32_t mipLevel = 0; mipLevel < roughnessLevels; ++mipLevel) {
                commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, PushConstant { mipLevel });
                commandBuffer.dispatch(
                    divCeil(prefilteredmapSize >> mipLevel, 16U),
                    divCeil(prefilteredmapSize >> mipLevel, 16U),
                    6);
            }
        }
    };
}