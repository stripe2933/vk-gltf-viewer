module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pipeline.SphericalHarmonicCoefficientsSumComputer;

import std;
import vku;
export import vulkan_hpp;
import :math.extended_arithmetic;
import :shader.spherical_harmonic_coefficients_sum_comp;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class SphericalHarmonicCoefficientsSumComputer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer> {
            explicit DescriptorSetLayout(
                const vk::raii::Device &device [[clang::lifetimebound]]
            ) : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer> {
                    device,
                    vk::DescriptorSetLayoutCreateInfo {
                        {},
                        vku::unsafeProxy(vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute }),
                    },
                } { }
        };

        struct PushConstant {
            std::uint32_t srcOffset;
            std::uint32_t count;
            std::uint32_t dstOffset;
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit SphericalHarmonicCoefficientsSumComputer(
            const vk::raii::Device &device [[clang::lifetimebound]]
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
                    vku::Shader { shader::spherical_harmonic_coefficients_sum_comp, vk::ShaderStageFlagBits::eCompute }).get()[0],
                *pipelineLayout,
            } } { }

        [[nodiscard]] auto compute(
            vk::CommandBuffer commandBuffer,
            vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
            PushConstant pushConstant
        ) const -> std::uint32_t {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {});

            while (true) {
                commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
                commandBuffer.dispatch(math::divCeil(pushConstant.count, 256U), 1, 1);

                pushConstant.count = math::divCeil(pushConstant.count, 256U);
                if (pushConstant.count == 1U) {
                    return pushConstant.dstOffset; // Return the offset that contains the sum result.
                }

                std::swap(pushConstant.srcOffset, pushConstant.dstOffset);
                commandBuffer.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                    {},
                    vk::MemoryBarrier {
                        vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                    },
                    {}, {});
            }
        }

        [[nodiscard]] static auto getPingPongBufferElementCount(
            std::uint32_t elementCount
        ) noexcept -> std::uint32_t {
            return elementCount + math::divCeil(elementCount, 256U);
        }
    };
}