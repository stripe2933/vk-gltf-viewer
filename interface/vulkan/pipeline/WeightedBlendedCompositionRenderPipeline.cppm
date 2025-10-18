module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.WeightedBlendedCompositionRenderPipeline;

import std;
import vku;
export import vulkan_hpp;

import vk_gltf_viewer.shader.screen_quad_vert;
import vk_gltf_viewer.shader.weighted_blended_composition_frag;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pipeline_layout.WeightedBlendedComposition;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct WeightedBlendedCompositionRenderPipeline final : vk::raii::Pipeline {
        WeightedBlendedCompositionRenderPipeline(
            const Gpu &gpu LIFETIMEBOUND,
            const pl::WeightedBlendedComposition &layout LIFETIMEBOUND,
            const rp::Scene &renderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::WeightedBlendedCompositionRenderPipeline::WeightedBlendedCompositionRenderPipeline(
    const Gpu &gpu,
    const pl::WeightedBlendedComposition &layout,
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
                        ? std::span<const std::uint32_t> { shader::weighted_blended_composition_frag<1> }
                        : std::span<const std::uint32_t> { shader::weighted_blended_composition_frag<0> }),
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
        &vku::lvalue(vk::PipelineColorBlendStateCreateInfo {
            {},
            false, {},
            vku::lvalue(vk::PipelineColorBlendAttachmentState {
                true,
                vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
            }),
            { 1.f, 1.f, 1.f, 1.f },
        }),
        &vku::lvalue(vk::PipelineDynamicStateCreateInfo {
            {},
            vku::lvalue({ vk::DynamicState::eViewport, vk::DynamicState::eScissor }),
        }),
        *layout,
        *renderPass, 2,
    } } { }