module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.CubemapToneMappingRenderPipeline;

import std;

import vk_gltf_viewer.shader.cubemap_tone_mapping_frag;
import vk_gltf_viewer.shader.screen_quad_vert;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.render_pass.CubemapToneMapping;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class CubemapToneMappingRenderPipeline {
    public:
        using DescriptorSetLayout = vku::raii::DescriptorSetLayout<vk::DescriptorType::eSampledImage>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        CubemapToneMappingRenderPipeline(
            const Gpu &gpu LIFETIMEBOUND,
            const rp::CubemapToneMapping &renderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::CubemapToneMappingRenderPipeline::CubemapToneMappingRenderPipeline(
    const Gpu &gpu,
    const rp::CubemapToneMapping &renderPass
) : descriptorSetLayout { gpu.device, vk::DescriptorSetLayoutCreateInfo {
        vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        vku::lvalue(DescriptorSetLayout::getCreateInfoBinding<0>(1, vk::ShaderStageFlagBits::eFragment)),
    } },
    pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
    } },
    pipeline { gpu.device, nullptr, vk::GraphicsPipelineCreateInfo {
        {},
        vku::lvalue({
            vk::PipelineShaderStageCreateInfo {
                {},
                vk::ShaderStageFlagBits::eVertex,
                *vku::lvalue(vk::raii::ShaderModule { gpu.device, vk::ShaderModuleCreateInfo {
                    {},
                    shader::screen_quad_vert,
                } }),
                "main",
            },
            vk::PipelineShaderStageCreateInfo {
                {},
                vk::ShaderStageFlagBits::eFragment,
                *vku::lvalue(vk::raii::ShaderModule { gpu.device, vk::ShaderModuleCreateInfo {
                    {},
                    vku::lvalue(gpu.supportShaderTrinaryMinMax
                        ? std::span<const std::uint32_t> { shader::cubemap_tone_mapping_frag<1> }
                        : std::span<const std::uint32_t> { shader::cubemap_tone_mapping_frag<0> }),
                } }),
                "main",
            },
        }),
        &vku::lvalue(vk::PipelineVertexInputStateCreateInfo{}),
        &vku::lvalue(vku::defaultPipelineInputAssemblyState(vk::PrimitiveTopology::eTriangleList)),
        nullptr,
        &vku::lvalue(vk::PipelineViewportStateCreateInfo {
            {},
            1, nullptr,
            1, nullptr,
        }),
        &vku::lvalue(vku::defaultPipelineRasterizationState()),
        &vku::lvalue(vk::PipelineMultisampleStateCreateInfo { {}, vk::SampleCountFlagBits::e1 }),
        nullptr,
        &vku::lvalue(vku::defaultPipelineColorBlendState(1)),
        &vku::lvalue(vk::PipelineDynamicStateCreateInfo {
            {},
            vku::lvalue({ vk::DynamicState::eViewport, vk::DynamicState::eScissor }),
        }),
        *pipelineLayout,
        *renderPass, 0,
    } } { }