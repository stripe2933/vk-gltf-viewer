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
) : Pipeline { gpu.device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
        createPipelineStages(
            gpu.device,
            vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
            vku::Shader {
                gpu.supportShaderTrinaryMinMax
                    ? std::span<const std::uint32_t> { shader::bloom_apply_frag<1> }
                    : std::span<const std::uint32_t> { shader::bloom_apply_frag<0> },
                vk::ShaderStageFlagBits::eFragment,
            }).get(),
        *layout, 1)
        .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
            {},
            false, false,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone, {},
            {}, {}, {}, {},
            1.f,
        }))
        .setRenderPass(*renderPass)
        .setSubpass(0),
    } { }
