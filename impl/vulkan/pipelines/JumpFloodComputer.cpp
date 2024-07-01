module;

#include <array>
#include <bit>
#include <compare>
#include <format>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.JumpFloodComputer;

import vku;

// language=comp
std::string_view vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::comp = R"comp(
#version 450

const uint UINT_MAX = 4294967295U;
const ivec2 normalizedOffsets[9] = ivec2[](
    ivec2(-1, -1), ivec2(0, -1), ivec2(1, -1),
    ivec2(-1,  0), ivec2(0,  0), ivec2(1,  0),
    ivec2(-1,  1), ivec2(0,  1), ivec2(1,  1)
);

layout (set = 0, binding = 0, rg16ui) uniform uimage2D pingPongImages[2];

layout (push_constant, std430) uniform PushConstant {
    bool forward;
    uint sampleOffset;
} pc;

layout (local_size_x = 16, local_size_y = 16) in;

uint length2(uvec2 v) {
    return v.x * v.x + v.y * v.y;
}

void main(){
    uvec2 imageExtent = imageSize(pingPongImages[0]);
    if (gl_GlobalInvocationID.x >= imageExtent.x || gl_GlobalInvocationID.y >= imageExtent.y) {
        return;
    }

    uvec2 closestSeedCoord;
    uint closestSeedDistanceSq = UINT_MAX;
    for (uint i = 0; i < 9; ++i){
        uvec2 seedCoord = imageLoad(pingPongImages[uint(!pc.forward)], ivec2(gl_GlobalInvocationID.xy) + int(pc.sampleOffset) * normalizedOffsets[i]).xy;
        if (seedCoord != uvec2(0U)) {
            uint seedDistanceSq = length2(seedCoord - gl_GlobalInvocationID.xy);
            if (seedDistanceSq < closestSeedDistanceSq) {
                closestSeedDistanceSq = seedDistanceSq;
                closestSeedCoord = seedCoord;
            }
        }
    }

    imageStore(pingPongImages[uint(pc.forward)], ivec2(gl_GlobalInvocationID.xy), uvec4(closestSeedDistanceSq == UINT_MAX ? uvec2(0) : closestSeedCoord, 0, 0));
}
)comp";

vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 2, vk::ShaderStageFlagBits::eCompute },
            }),
        },
    } { }

vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::JumpFloodComputer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t initialSampleOffset,
    const vk::Extent2D &imageExtent
) const -> vk::Bool32 {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});

    PushConstant pushConstant {
        .forward = true,
        .sampleOffset = initialSampleOffset,
    };
    for (; pushConstant.sampleOffset > 0U; pushConstant.forward = !pushConstant.forward, pushConstant.sampleOffset >>= 1U) {
        commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
        commandBuffer.dispatch(
            vku::divCeil(imageExtent.width, 16U),
            vku::divCeil(imageExtent.height, 16U),
            1);

        if (pushConstant.sampleOffset != 1U) {
            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                {},
                vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                {}, {});
        }
    }
    return pushConstant.forward;
}

auto vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::createPipelineLayout(
    const vk::raii::Device &device
) const -> decltype(pipelineLayout) {
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

auto vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> decltype(pipeline) {
    return { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(vku::createPipelineStages(
            device,
            vku::Shader { compiler, comp, vk::ShaderStageFlagBits::eCompute }).get()),
        *pipelineLayout,
    } };
}