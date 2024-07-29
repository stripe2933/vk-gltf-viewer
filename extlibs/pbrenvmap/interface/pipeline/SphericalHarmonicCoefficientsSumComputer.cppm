module;

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module pbrenvmap:pipeline.SphericalHarmonicCoefficientsSumComputer;

import std;
import vku;
export import vulkan_hpp;

namespace pbrenvmap::pipeline {
    export class SphericalHarmonicCoefficientsSumComputer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1> {
            explicit DescriptorSetLayouts(const vk::raii::Device &device);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                const vk::DescriptorBufferInfo &pingPongBufferInfo [[clang::lifetimebound]]
            ) const -> std::array<vk::WriteDescriptorSet, 1>;
        };

        struct PushConstant {
            std::uint32_t srcOffset;
            std::uint32_t count;
            std::uint32_t dstOffset;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SphericalHarmonicCoefficientsSumComputer(const vk::raii::Device &device, const shaderc::Compiler &compiler);

        [[nodiscard]] auto compute(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, PushConstant pushConstant) const -> std::uint32_t;

        [[nodiscard]] static auto getPingPongBufferElementCount(std::uint32_t elementCount) noexcept -> std::uint32_t;

    private:
        static std::string_view comp;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> vk::raii::PipelineLayout;
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> vk::raii::Pipeline;
    };
}

// module :private;

// language=comp
std::string_view pbrenvmap::pipeline::SphericalHarmonicCoefficientsSumComputer::comp = R"comp(
#version 450
#extension GL_EXT_scalar_block_layout : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

const vec3[9] _9_ZERO_VEC3S = vec3[9](vec3(0), vec3(0), vec3(0), vec3(0), vec3(0), vec3(0), vec3(0), vec3(0), vec3(0));

layout (set = 0, binding = 0, scalar) buffer PingPongBuffer {
    vec3 data[][9];
};

layout (push_constant, std430) uniform PushConstant{
    uint srcOffset;
    uint count;
    uint dstOffset;
} pc;

layout (local_size_x = 256) in;

shared vec3 subgroupReduction[32][9]; // gl_NumSubgroups ≤ 32 (subgroup size must be at least 8).

void main(){
    vec3 pingPongData[] = gl_GlobalInvocationID.x < pc.count ? data[pc.srcOffset + gl_GlobalInvocationID.x] : _9_ZERO_VEC3S;
    vec3 reductions[] = vec3[9](
        subgroupAdd(pingPongData[0]),
        subgroupAdd(pingPongData[1]),
        subgroupAdd(pingPongData[2]),
        subgroupAdd(pingPongData[3]),
        subgroupAdd(pingPongData[4]),
        subgroupAdd(pingPongData[5]),
        subgroupAdd(pingPongData[6]),
        subgroupAdd(pingPongData[7]),
        subgroupAdd(pingPongData[8])
    );
    if (subgroupElect()){
        subgroupReduction[gl_SubgroupID] = reductions;
    }

    memoryBarrierShared();
    barrier();

    // For subgroup size 8, use subgroup whose ID is 0..4 to reduce the data one more time.
    // TODO: this code is not tested yet.
    if ((gl_SubgroupSize == 8U) && (gl_SubgroupID < 4U)){
        reductions = vec3[9](
            subgroupAdd(pingPongData[0]),
            subgroupAdd(pingPongData[1]),
            subgroupAdd(pingPongData[2]),
            subgroupAdd(pingPongData[3]),
            subgroupAdd(pingPongData[4]),
            subgroupAdd(pingPongData[5]),
            subgroupAdd(pingPongData[6]),
            subgroupAdd(pingPongData[7]),
            subgroupAdd(pingPongData[8])
        );
        if (subgroupElect()){
            subgroupReduction[gl_SubgroupID] = reductions;
        }

        memoryBarrierShared();
        barrier();
    }
    uint pingPongDataElementCount = (gl_SubgroupSize == 8U) ? 4U : gl_NumSubgroups;

    if (gl_SubgroupID == 0U){
        pingPongData = gl_SubgroupInvocationID < pingPongDataElementCount ? subgroupReduction[gl_SubgroupInvocationID] : _9_ZERO_VEC3S;
        // TODO: Following code compile successfully in glslc, but failed in SPIRV-Cross (SPIR-V -> MSL). Fix when available.
        // data[pc.dstOffset + gl_WorkGroupID.x] = vec3[](
        //     subgroupAdd(pingPongData[0]),
        //     subgroupAdd(pingPongData[1]),
        //     subgroupAdd(pingPongData[2]),
        //     subgroupAdd(pingPongData[3]),
        //     subgroupAdd(pingPongData[4]),
        //     subgroupAdd(pingPongData[5]),
        //     subgroupAdd(pingPongData[6]),
        //     subgroupAdd(pingPongData[7]),
        //     subgroupAdd(pingPongData[8])
        // );
        data[pc.dstOffset + gl_WorkGroupID.x][0] = subgroupAdd(pingPongData[0]);
        data[pc.dstOffset + gl_WorkGroupID.x][1] = subgroupAdd(pingPongData[1]);
        data[pc.dstOffset + gl_WorkGroupID.x][2] = subgroupAdd(pingPongData[2]);
        data[pc.dstOffset + gl_WorkGroupID.x][3] = subgroupAdd(pingPongData[3]);
        data[pc.dstOffset + gl_WorkGroupID.x][4] = subgroupAdd(pingPongData[4]);
        data[pc.dstOffset + gl_WorkGroupID.x][5] = subgroupAdd(pingPongData[5]);
        data[pc.dstOffset + gl_WorkGroupID.x][6] = subgroupAdd(pingPongData[6]);
        data[pc.dstOffset + gl_WorkGroupID.x][7] = subgroupAdd(pingPongData[7]);
        data[pc.dstOffset + gl_WorkGroupID.x][8] = subgroupAdd(pingPongData[8]);
    }
}
)comp";

template <std::unsigned_integral T>
[[nodiscard]] constexpr auto divCeil(T num, T denom) noexcept -> T {
    return (num / denom) + (num % denom != 0);
}

pbrenvmap::pipeline::SphericalHarmonicCoefficientsSumComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
            }),
        },
    } { }

auto pbrenvmap::pipeline::SphericalHarmonicCoefficientsSumComputer::DescriptorSets::getDescriptorWrites0(
    const vk::DescriptorBufferInfo &pingPongBufferInfo
) const -> std::array<vk::WriteDescriptorSet, 1> {
    return std::array {
        getDescriptorWrite<0, 0>().setBufferInfo(pingPongBufferInfo),
    };
}

pbrenvmap::pipeline::SphericalHarmonicCoefficientsSumComputer::SphericalHarmonicCoefficientsSumComputer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto pbrenvmap::pipeline::SphericalHarmonicCoefficientsSumComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    PushConstant pushConstant
) const -> std::uint32_t {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});

    while (true) {
        commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
        commandBuffer.dispatch(divCeil(pushConstant.count, 256U), 1, 1);

        pushConstant.count = divCeil(pushConstant.count, 256U);
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

auto pbrenvmap::pipeline::SphericalHarmonicCoefficientsSumComputer::getPingPongBufferElementCount(
    std::uint32_t elementCount
) noexcept -> std::uint32_t {
    return elementCount + divCeil(elementCount, 256U);
}

auto pbrenvmap::pipeline::SphericalHarmonicCoefficientsSumComputer::createPipelineLayout(
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

auto pbrenvmap::pipeline::SphericalHarmonicCoefficientsSumComputer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
    ) const -> vk::raii::Pipeline {
    return { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(vku::createPipelineStages(
            device,
            vku::Shader { compiler, comp, vk::ShaderStageFlagBits::eCompute }).get()),
        *pipelineLayout,
    } };
}