module;

#include <version>

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pipeline.SubgroupMipmapComputer;

import std;
import :helpers.ranges;
import :shader.subgroup_mipmap_comp;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class SubgroupMipmapComputer {
        struct PushConstant {
            std::int32_t baseLevel;
            std::uint32_t remainingMipLevels;
        };

    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
            DescriptorSetLayout(
                const Gpu &gpu [[clang::lifetimebound]],
                std::uint32_t mipImageCount
            ) : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
                gpu.device,
                vk::DescriptorSetLayoutCreateInfo {
                    {},
                    vku::unsafeProxy(getBindings({ gpu.supportShaderImageLoadStoreLod ? 1U : mipImageCount, vk::ShaderStageFlagBits::eCompute })),
                },
            } { }
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SubgroupMipmapComputer(
            const Gpu &gpu [[clang::lifetimebound]],
            std::uint32_t mipImageCount
        ) : descriptorSetLayout { gpu, mipImageCount },
            pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(PushConstant),
                }),
            } },
            pipeline { gpu.device, nullptr, vk::ComputePipelineCreateInfo {
                {},
                createPipelineStages(
                    gpu.device,
                    vku::Shader {
                        gpu.subgroupSize == 16U
                            ? gpu.supportShaderImageLoadStoreLod
                                ? std::span<const std::uint32_t> { shader::subgroup_mipmap_comp<16, 1> }
                                : std::span<const std::uint32_t> { shader::subgroup_mipmap_comp<16, 0> }
                            : gpu.subgroupSize == 32U
                                ? gpu.supportShaderImageLoadStoreLod
                                    ? std::span<const std::uint32_t> { shader::subgroup_mipmap_comp<32, 1> }
                                    : std::span<const std::uint32_t> { shader::subgroup_mipmap_comp<32, 0> }
                                : gpu.supportShaderImageLoadStoreLod
                                    ? std::span<const std::uint32_t> { shader::subgroup_mipmap_comp<64, 1> }
                                    : std::span<const std::uint32_t> { shader::subgroup_mipmap_comp<64, 0> },
                        vk::ShaderStageFlagBits::eCompute,
                    }).get()[0],
                *pipelineLayout,
            } } { }

        auto compute(
            vk::CommandBuffer commandBuffer,
            vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
            const vk::Extent2D &baseImageExtent,
            std::uint32_t mipLevels
        ) const -> void {
            // Base image size must be greater than or equal to 32. Therefore, the first execution may process less than 5 mip levels.
            // For example, if base extent is 4096x4096 (mipLevels=13),
            // Step 0 (4096 -> 1024)
            // Step 1 (1024 -> 32)
            // Step 2 (32 -> 1) (full processing required)

#if __cpp_lib_ranges_chunk >= 202202L
             const std::vector indexChunks
                 = std::views::iota(1, static_cast<std::int32_t>(mipLevels))               // [1, 2, ..., 11, 12]
                 | std::views::reverse                                                     // [12, 11, ..., 2, 1]
                 | std::views::chunk(5)                                                    // [[12, 11, 10, 9, 8], [7, 6, 5, 4, 3], [2, 1]]
                 | std::views::transform([](auto &&chunk) {
                      return chunk | std::views::reverse | std::ranges::to<std::vector>();
                 })                                                                        // [[8, 9, 10, 11, 12], [3, 4, 5, 6, 7], [1, 2]]
                 | std::views::reverse                                                     // [[1, 2], [3, 4, 5, 6, 7], [8, 9, 10, 11, 12]]
                 | std::ranges::to<std::vector>();
#else
            std::vector<std::vector<std::int32_t>> indexChunks;
            for (int endMipLevel = mipLevels; endMipLevel > 1; endMipLevel -= 5) {
                indexChunks.emplace_back(std::from_range, std::views::iota(std::max(1, endMipLevel - 5), endMipLevel));
            }
            std::ranges::reverse(indexChunks);
#endif

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {});
            for (const auto &[idx, mipIndices] : indexChunks | ranges::views::enumerate) {
                if (idx != 0) {
                    commandBuffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                        {},
                        vk::MemoryBarrier {
                            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                        },
                        {}, {});
                }

                commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, PushConstant {
                    mipIndices.front() - 1,
                    static_cast<std::uint32_t>(mipIndices.size()),
                });
                commandBuffer.dispatch(
                    (baseImageExtent.width >> mipIndices.front()) / 16U,
                    (baseImageExtent.height >> mipIndices.front()) / 16U,
                    6);
            }
        }
    };
}