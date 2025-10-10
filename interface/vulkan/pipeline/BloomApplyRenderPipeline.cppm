module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.BloomApplyRenderPipeline;

import std;
import vku;
export import vulkan_hpp;

import vk_gltf_viewer.shader.bloom_apply_frag;
import vk_gltf_viewer.shader.screen_quad_vert;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pipeline_layout.BloomApply;
export import vk_gltf_viewer.vulkan.render_pass.BloomApply;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct BloomApplyRenderPipeline final : vk::raii::Pipeline {
        BloomApplyRenderPipeline(
            const Gpu &gpu LIFETIMEBOUND,
            const pl::BloomApply &layout LIFETIMEBOUND,
            const rp::BloomApply &renderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::BloomApplyRenderPipeline::BloomApplyRenderPipeline(
    const Gpu &gpu,
    const pl::BloomApply &layout,
    const rp::BloomApply &renderPass
) : Pipeline { gpu.device, nullptr, vk::GraphicsPipelineCreateInfo {
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
                        ? std::span<const std::uint32_t> { shader::bloom_apply_frag<1> }
                        : std::span<const std::uint32_t> { shader::bloom_apply_frag<0> }),
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
        *layout,
        *renderPass, 0,
    } } { }
