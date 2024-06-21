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
    uvec2 offset;
    uvec2 extent;
    bool forward;
    uint sampleOffset;
} pc;

layout (local_size_x = 16, local_size_y = 16) in;

uint length2(uvec2 v) {
    return v.x * v.x + v.y * v.y;
}

void main(){
    if (gl_GlobalInvocationID.x >= pc.extent.x || gl_GlobalInvocationID.y >= pc.extent.y) {
        return;
    }

    uvec2 closestSeedCoordXy, closestSeedCoordZw;
    uint closestSeedDistanceSqXy = UINT_MAX, closestSeedDistanceSqZw = UINT_MAX;
    for (uint i = 0; i < 9; ++i){
        uvec4 seedCoord = imageLoad(pingPongImages[uint(!pc.forward)], ivec2(pc.offset + gl_GlobalInvocationID.xy) + int(pc.sampleOffset) * normalizedOffsets[i]);
        uvec2 seedCoordXy = seedCoord.xy, seedCoordZw = seedCoord.zw;
        if (seedCoordXy != uvec2(0U)) {
            uint seedDistanceSq = length2(seedCoordXy - gl_GlobalInvocationID.xy);
            if (seedDistanceSq < closestSeedDistanceSqXy) {
                closestSeedDistanceSqXy = seedDistanceSq;
                closestSeedCoordXy = seedCoordXy;
            }
        }
        if (seedCoordZw != uvec2(0U)) {
            uint seedDistanceSq = length2(seedCoordZw - gl_GlobalInvocationID.xy);
            if (seedDistanceSq < closestSeedDistanceSqZw) {
                closestSeedDistanceSqZw = seedDistanceSq;
                closestSeedCoordZw = seedCoordZw;
            }
        }
    }

    imageStore(pingPongImages[uint(pc.forward)], ivec2(pc.offset + gl_GlobalInvocationID.xy), uvec4(
        closestSeedDistanceSqXy == UINT_MAX ? uvec2(0) : closestSeedCoordXy,
        closestSeedDistanceSqZw == UINT_MAX ? uvec2(0) : closestSeedCoordZw));
}
)comp";

vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> { device, LayoutBindings {
        {},
        vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 2, vk::ShaderStageFlagBits::eCompute },
    } } { }

vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::JumpFloodComputer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    const vk::Rect2D &computeRegion
) const -> vk::Bool32 {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});

    PushConstant pushConstant {
        .offset = { computeRegion.offset.x, computeRegion.offset.y },
        .extent = { computeRegion.extent.width, computeRegion.extent.height },
        .forward = true,
        .sampleOffset = std::bit_floor(std::min(computeRegion.extent.width, computeRegion.extent.height)) >> 1U,
    };
    for (bool first = true; pushConstant.sampleOffset > 0U; pushConstant.forward = !pushConstant.forward, pushConstant.sampleOffset >>= 1U) {
        if (first) {
            commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
            first = false;
        }
        else {
            // Since offset and extent is not changed after first push, we can optimize by pushing only data from `forward` field.
            commandBuffer.pushConstants(
                *pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                offsetof(PushConstant, forward), sizeof(PushConstant) - offsetof(PushConstant, forward), &pushConstant.forward);
        }
        commandBuffer.dispatch(
            vku::divCeil(computeRegion.extent.width, 16U),
            vku::divCeil(computeRegion.extent.height, 16U),
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
    constexpr vk::PushConstantRange pushConstantRange {
        vk::ShaderStageFlagBits::eCompute,
        0, sizeof(PushConstant),
    };
    return { device, vk::PipelineLayoutCreateInfo {
        {},
        descriptorSetLayouts,
        pushConstantRange,
    } };
}

auto vk_gltf_viewer::vulkan::pipelines::JumpFloodComputer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> decltype(pipeline) {
    const auto [_, stages] = createStages(
        device,
        vku::Shader { compiler, comp, vk::ShaderStageFlagBits::eCompute });

    return { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(stages),
        *pipelineLayout,
    } };
}