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
) : Pipeline { device, nullptr, vk::GraphicsPipelineCreateInfo {
        {},
        vku::lvalue({
            vk::PipelineShaderStageCreateInfo {
                {},
                vk::ShaderStageFlagBits::eVertex,
                *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                    {},
                    shader::skybox_vert,
                } }),
                "main",
            },
            vk::PipelineShaderStageCreateInfo {
                {},
                vk::ShaderStageFlagBits::eFragment,
                *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                    {},
                    shader::skybox_frag,
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
        &vku::lvalue(vk::PipelineMultisampleStateCreateInfo { {}, vk::SampleCountFlagBits::e4 }),
        &vku::lvalue(vk::PipelineDepthStencilStateCreateInfo {
            {},
            true, false, vk::CompareOp::eEqual,
        }),
        &vku::lvalue(vku::defaultPipelineColorBlendState(1)),
        &vku::lvalue(vk::PipelineDynamicStateCreateInfo {
            {},
            vku::lvalue({ vk::DynamicState::eViewport, vk::DynamicState::eScissor }),
        }),
        *layout,
        *renderPass, 0,
    } } { }