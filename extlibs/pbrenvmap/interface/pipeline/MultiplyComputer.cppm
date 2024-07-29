module;

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module pbrenvmap:pipeline.MultiplyComputer;

import std;
import vku;
export import vulkan_hpp;

namespace pbrenvmap::pipeline {
    export class MultiplyComputer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<2> {
            explicit DescriptorSetLayouts(const vk::raii::Device &device);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                const vk::DescriptorBufferInfo &srcBufferInfo [[clang::lifetimebound]],
                const vk::DescriptorBufferInfo &dstBufferInfo [[clang::lifetimebound]]
            ) const -> std::array<vk::WriteDescriptorSet, 2>;
        };

        struct PushConstant {
            std::uint32_t numCount;
            float multiplier;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        MultiplyComputer(const vk::raii::Device &device, const shaderc::Compiler &compiler);

        auto compute(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const PushConstant &pushConstant) const -> void;

    private:
        static std::string_view comp;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> vk::raii::PipelineLayout;
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> vk::raii::Pipeline;
    };
}

// module :private;

// language=comp
std::string_view pbrenvmap::pipeline::MultiplyComputer::comp = R"comp(
#version 450

layout (set = 0, binding = 0) readonly buffer ReadonlyFloatBuffer {
    float srcBuffer[];
};
layout (set = 0, binding = 1) writeonly buffer WriteonlyFloatBuffer {
    float dstBuffer[];
};

layout (push_constant, std430) uniform PushConstant {
    uint numCount;
    float multiplier;
} pc;

layout (local_size_x = 256) in;

void main(){
    if (gl_GlobalInvocationID.x < pc.numCount) {
        dstBuffer[gl_GlobalInvocationID.x] = srcBuffer[gl_GlobalInvocationID.x] * pc.multiplier;
    }
}
)comp";

template <std::unsigned_integral T>
[[nodiscard]] constexpr auto divCeil(T num, T denom) noexcept -> T {
    return (num / denom) + (num % denom != 0);
}

pbrenvmap::pipeline::MultiplyComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<2> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
                vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
            }),
        },
    } { }

auto pbrenvmap::pipeline::MultiplyComputer::DescriptorSets::getDescriptorWrites0(
    const vk::DescriptorBufferInfo &srcBufferInfo,
    const vk::DescriptorBufferInfo &dstBufferInfo
) const -> std::array<vk::WriteDescriptorSet, 2> {
    return std::array {
        getDescriptorWrite<0, 0>().setBufferInfo(srcBufferInfo),
        getDescriptorWrite<0, 1>().setBufferInfo(dstBufferInfo),
    };
}

pbrenvmap::pipeline::MultiplyComputer::MultiplyComputer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto pbrenvmap::pipeline::MultiplyComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
    commandBuffer.dispatch(divCeil(pushConstant.numCount, 256U), 1, 1);
}

auto pbrenvmap::pipeline::MultiplyComputer::createPipelineLayout(
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

auto pbrenvmap::pipeline::MultiplyComputer::createPipeline(
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