module;

#include <cstdint>
#include <algorithm>
#include <array>
#include <compare>
#include <format>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module pbrenvmap:pipelines.SubgroupMipmapComputer;

import vku;
export import vulkan_hpp;
import :details.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace pbrenvmap::pipelines {
    export class SubgroupMipmapComputer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1> {
            explicit DescriptorSetLayouts(const vk::raii::Device &device, std::uint32_t mipImageCount);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                auto &&mipImageViews
            ) const noexcept {
                return vku::RefHolder {
                    [this](std::span<const vk::DescriptorImageInfo> imageInfos) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setImageInfo(imageInfos),
                        };
                    },
                    std::vector { std::from_range, FWD(mipImageViews) | std::views::transform([](vk::ImageView imageView) {
                        return vk::DescriptorImageInfo { {}, imageView, vk::ImageLayout::eGeneral };
                    }) },
                };
            }
        };

        struct PushConstant {
            std::uint32_t baseLevel;
            std::uint32_t remainingMipLevels;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SubgroupMipmapComputer(const vk::raii::Device &device, std::uint32_t mipImageCount, std::uint32_t subgroupSize, const shaderc::Compiler &compiler);

        auto compute(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const vk::Extent2D &baseImageExtent, std::uint32_t mipLevels) const -> void;

    private:
        static std::string_view comp;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> vk::raii::PipelineLayout;
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, std::uint32_t subgroupSize, const shaderc::Compiler &compiler) const -> vk::raii::Pipeline;
    };
}

// module :private;

// TODO: support for different subgroup sizes (current is based on 32).
// language=comp
std::string_view pbrenvmap::pipelines::SubgroupMipmapComputer::comp = R"comp(
#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_KHR_shader_subgroup_shuffle : require

layout (set = 0, binding = 0, rgba32f) uniform imageCube mipImages[];

layout (push_constant) uniform PushConstant {
    uint baseLevel;
    uint remainingMipLevels;
} pc;

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

shared vec4 sharedData[8];

void main(){
    ivec2 sampleCoordinate = ivec2(gl_WorkGroupSize.xy * gl_WorkGroupID.xy + uvec2(
        (gl_LocalInvocationID.x & 7U) | (gl_LocalInvocationID.y & ~7U),
        ((gl_LocalInvocationID.y << 1U) | (gl_LocalInvocationID.x >> 3U)) & 15U
    ));

    vec4 averageColor
        = imageLoad(mipImages[pc.baseLevel], ivec3(2 * sampleCoordinate, gl_GlobalInvocationID.z))
        + imageLoad(mipImages[pc.baseLevel], ivec3(2 * sampleCoordinate + ivec2(1, 0), gl_GlobalInvocationID.z))
        + imageLoad(mipImages[pc.baseLevel], ivec3(2 * sampleCoordinate + ivec2(0, 1), gl_GlobalInvocationID.z))
        + imageLoad(mipImages[pc.baseLevel], ivec3(2 * sampleCoordinate + ivec2(1, 1), gl_GlobalInvocationID.z));
    averageColor /= 4.0;
    imageStore(mipImages[pc.baseLevel + 1U], ivec3(sampleCoordinate, gl_GlobalInvocationID.z), averageColor);
    if (pc.remainingMipLevels == 1U){
        return;
    }

    averageColor += subgroupShuffleXor(averageColor, 1U /* 0b0001 */);
    averageColor += subgroupShuffleXor(averageColor, 8U /* 0b1000 */);
    averageColor /= 4.f;
    if ((gl_SubgroupInvocationID & 9U /* 0b1001 */) == 9U) {
        imageStore(mipImages[pc.baseLevel + 2U], ivec3(sampleCoordinate >> 1, gl_GlobalInvocationID.z), averageColor);
    }
    if (pc.remainingMipLevels == 2U){
        return;
    }

    averageColor += subgroupShuffleXor(averageColor, 2U /* 0b00010 */);
    averageColor += subgroupShuffleXor(averageColor, 16U /* 0b10000 */);
    averageColor /= 4.f;

    if ((gl_SubgroupInvocationID & 27U /* 0b11011 */) == 27U) {
        imageStore(mipImages[pc.baseLevel + 3U], ivec3(sampleCoordinate >> 2, gl_GlobalInvocationID.z), averageColor);
    }
    if (pc.remainingMipLevels == 3U){
        return;
    }

    averageColor += subgroupShuffleXor(averageColor, 4U /* 0b00100 */);
    if (subgroupElect()){
        sharedData[gl_SubgroupID] = averageColor;
    }

    memoryBarrierShared();
    barrier();

    if ((gl_SubgroupID & 1U) == 1U){
        averageColor = (sharedData[gl_SubgroupID] + sharedData[gl_SubgroupID ^ 1U]) / 4.f;
        imageStore(mipImages[pc.baseLevel + 4U], ivec3(sampleCoordinate >> 3, gl_GlobalInvocationID.z), averageColor);
    }
    if (pc.remainingMipLevels == 4U){
        return;
    }

    if (gl_LocalInvocationIndex == 0U){
        averageColor = (sharedData[0] + sharedData[1] + sharedData[2] + sharedData[3] + sharedData[4] + sharedData[5] + sharedData[6] + sharedData[7]) / 16.f;
        imageStore(mipImages[pc.baseLevel + 5U], ivec3(sampleCoordinate >> 4, gl_GlobalInvocationID.z), averageColor);
    }
}
)comp";

pbrenvmap::pipelines::SubgroupMipmapComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device,
    std::uint32_t mipImageCount
) : vku::DescriptorSetLayouts<1> {
        device,
        vk::StructureChain {
            vk::DescriptorSetLayoutCreateInfo {
                vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                vku::unsafeProxy({
                    vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, mipImageCount, vk::ShaderStageFlagBits::eCompute },
                }),
            },
            vk::DescriptorSetLayoutBindingFlagsCreateInfo {
                vku::unsafeProxy({
                    vk::Flags { vk::DescriptorBindingFlagBits::eUpdateAfterBind },
                }),
            },
        }.get(),
    } { }

pbrenvmap::pipelines::SubgroupMipmapComputer::SubgroupMipmapComputer(
    const vk::raii::Device &device,
    std::uint32_t mipImageCount,
    std::uint32_t subgroupSize,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device, mipImageCount },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, subgroupSize, compiler) } { }

auto pbrenvmap::pipelines::SubgroupMipmapComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    const vk::Extent2D &baseImageExtent,
    std::uint32_t mipLevels
) const -> void {
    // Base image size must be greater than or equal to 32. Therefore, the first execution may process less than 5 mip levels.
    // For example, if base extent is 4096x4096 (mipLevels=13),
    // Step 0 (4096 -> 1024)
    // Step 1 (1024 -> 32)
    // Step 2 (32 -> 1) (full processing required)

    // TODO.CXX23: use std::views::chunk instead, like:
    // const std::vector indexChunks
    //     = std::views::iota(1U, targetImage.mipLevels)                             // [1, 2, ..., 11, 12]
    //     | std::views::reverse                                                     // [12, 11, ..., 2, 1]
    //     | std::views::chunk(5)                                                    // [[12, 11, 10, 9, 8], [7, 6, 5, 4, 3], [2, 1]]
    //     | std::views::transform([](auto &&chunk) {
    //          return chunk | std::views::reverse | std::ranges::to<std::vector>();
    //     })                                                                        // [[8, 9, 10, 11, 12], [3, 4, 5, 6, 7], [1, 2]]
    //     | std::views::reverse                                                     // [[1, 2], [3, 4, 5, 6, 7], [8, 9, 10, 11, 12]]
    //     | std::ranges::to<std::vector>();
    std::vector<std::vector<std::uint32_t>> indexChunks;
    for (int endMipLevel = mipLevels; endMipLevel > 1; endMipLevel -= 5) {
        indexChunks.emplace_back(
            std::views::iota(
                static_cast<std::uint32_t>(std::max(1, endMipLevel - 5)),
                static_cast<std::uint32_t>(endMipLevel))
            | std::ranges::to<std::vector<std::uint32_t>>());
    }
    std::ranges::reverse(indexChunks);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});
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
            mipIndices.front() - 1U,
            static_cast<std::uint32_t>(mipIndices.size()),
        });
        commandBuffer.dispatch(
            (baseImageExtent.width >> mipIndices.front()) / 16U,
            (baseImageExtent.height >> mipIndices.front()) / 16U,
            6);
    }
}

auto pbrenvmap::pipelines::SubgroupMipmapComputer::createPipelineLayout(
    const vk::raii::Device &device
) const -> vk::raii::PipelineLayout {
    return { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy(descriptorSetLayouts.getHandles()),
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eCompute,
                0, sizeof(PushConstant),
            },
        }),
    } };
}

auto pbrenvmap::pipelines::SubgroupMipmapComputer::createPipeline(
    const vk::raii::Device &device,
    std::uint32_t subgroupSize,
    const shaderc::Compiler &compiler
) const -> vk::raii::Pipeline {
    // TODO: support for different subgroup sizes (current is based on 32).
    const auto [_, stages] = vku::createStages(device,
        vku::Shader { compiler, comp, vk::ShaderStageFlagBits::eCompute });
    return { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(stages),
        *pipelineLayout,
    } };
}