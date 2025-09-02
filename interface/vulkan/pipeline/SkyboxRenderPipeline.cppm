module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.SkyboxRenderPipeline;

#ifdef _MSC_VER
import std;
#endif
import vku;
export import vulkan_hpp;

import vk_gltf_viewer.shader.skybox_frag;
import vk_gltf_viewer.shader.skybox_vert;
export import vk_gltf_viewer.vulkan.pipeline_layout.Skybox;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct SkyboxRenderPipeline : vk::raii::Pipeline {
        SkyboxRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::Skybox &layout LIFETIMEBOUND,
            const rp::Scene &renderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::SkyboxRenderPipeline::SkyboxRenderPipeline(
    const vk::raii::Device &device,
    const pl::Skybox &layout,
    const rp::Scene &renderPass
) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
        createPipelineStages(
            device,
            vku::Shader { shader::skybox_vert, vk::ShaderStageFlagBits::eVertex },
            vku::Shader { shader::skybox_frag, vk::ShaderStageFlagBits::eFragment }).get(),
        *layout, 1, true, vk::SampleCountFlagBits::e4)
        .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
            {},
            false, false,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone, {},
            {}, {}, {}, {},
            1.f,
        }))
        .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
            {},
            true, false, vk::CompareOp::eEqual,
        }))
        .setRenderPass(*renderPass)
        .setSubpass(0),
    } { }