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
            const pl::Outline &layout LIFETIMEBOUND,
            std::uint32_t viewMask
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::OutlineRenderPipeline::OutlineRenderPipeline(
    const vk::raii::Device &device,
    const pl::Outline &layout,
    std::uint32_t viewMask
) : Pipeline { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                vku::Shader { shader::outline_frag, vk::ShaderStageFlagBits::eFragment }).get(),
            *layout, 1)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eNone, {},
                {}, {}, {}, {},
                1.0f,
            }))
            .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                {},
                false, {},
                vku::unsafeProxy(vk::PipelineColorBlendAttachmentState {
                    true,
                    vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                    vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                }),
            { 1.f, 1.f, 1.f, 1.f },
        })),
        vk::PipelineRenderingCreateInfo {
            viewMask,
            vku::unsafeProxy(vk::Format::eB8G8R8A8Srgb),
        },
    }.get() } { }
