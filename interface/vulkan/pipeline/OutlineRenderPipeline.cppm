module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.OutlineRenderPipeline;

import std;
import vku;
export import vulkan_hpp;

import vk_gltf_viewer.shader.outline_frag;
import vk_gltf_viewer.shader.screen_quad_vert;
export import vk_gltf_viewer.vulkan.pipeline_layout.Outline;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct OutlineRenderPipeline final : vk::raii::Pipeline {
        OutlineRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::Outline &layout LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::OutlineRenderPipeline::OutlineRenderPipeline(
    const vk::raii::Device &device,
    const pl::Outline &layout
) : Pipeline { device, nullptr, vk::StructureChain {
        vk::GraphicsPipelineCreateInfo {
            {},
            vku::lvalue({
                vk::PipelineShaderStageCreateInfo {
                    {},
                    vk::ShaderStageFlagBits::eVertex,
                    *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                        {},
                        shader::screen_quad_vert,
                    } }),
                    "main",
                },
                vk::PipelineShaderStageCreateInfo {
                    {},
                    vk::ShaderStageFlagBits::eFragment,
                    *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                        {},
                        shader::outline_frag,
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
                    vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                }),
                { 1.f, 1.f, 1.f, 1.f },
            }),
            &vku::lvalue(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::lvalue({ vk::DynamicState::eViewport, vk::DynamicState::eScissor }),
            }),
            *layout,
        },
        vk::PipelineRenderingCreateInfo {
            {},
            vku::lvalue(vk::Format::eB8G8R8A8Srgb),
        },
    }.get() } { }
