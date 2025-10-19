module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.InverseToneMappingRenderPipeline;

import std;
import vku;
export import vulkan_hpp;

import vk_gltf_viewer.shader.inverse_tone_mapping_frag;
import vk_gltf_viewer.shader.screen_quad_vert;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pipeline_layout.InverseToneMapping;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct InverseToneMappingRenderPipeline final : vk::raii::Pipeline {
        InverseToneMappingRenderPipeline(
            const Gpu &gpu LIFETIMEBOUND,
            const pl::InverseToneMapping &layout LIFETIMEBOUND,
            const rp::Scene &renderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::InverseToneMappingRenderPipeline::InverseToneMappingRenderPipeline(
    const Gpu &gpu,
    const pl::InverseToneMapping &layout,
    const rp::Scene &renderPass
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
                        ? std::span<const std::uint32_t> { shader::inverse_tone_mapping_frag<1> }
                        : std::span<const std::uint32_t> { shader::inverse_tone_mapping_frag<0> }),
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
        // Only stencil test passed fragment's (which indicates the fragment's corresponding material's emissive
        // strength > 1.0) subpass input will be inverse tone mapped.
        &vku::lvalue(vk::PipelineDepthStencilStateCreateInfo {
            {},
            false, false, {}, false,
            true,
            vk::StencilOpState { vk::StencilOp::eKeep, vk::StencilOp::eKeep, {}, vk::CompareOp::eEqual, ~0U, {}, 1U },
            vk::StencilOpState { vk::StencilOp::eKeep, vk::StencilOp::eKeep, {}, vk::CompareOp::eEqual, ~0U, {}, 1U },
        }),
        &vku::lvalue(vku::defaultPipelineColorBlendState(1)),
        &vku::lvalue(vk::PipelineDynamicStateCreateInfo {
            {},
            vku::lvalue({ vk::DynamicState::eViewport, vk::DynamicState::eScissor }),
        }),
        *layout,
        *renderPass, 3,
    } } { }