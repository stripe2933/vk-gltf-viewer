module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.OutlineRenderer;

import std;
export import glm;
export import vku;

import vk_gltf_viewer.shader.outline_frag;
import vk_gltf_viewer.shader.screen_quad_vert;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct OutlineRenderer {
        struct PushConstant {
            glm::vec4 outlineColor;
            float outlineThickness;
        };

        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit OutlineRenderer(const vk::raii::Device &device LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::OutlineRenderer::OutlineRenderer(
    const vk::raii::Device &device
) : descriptorSetLayout {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy(DescriptorSetLayout::getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
        },
    },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo{
        {},
        *descriptorSetLayout,
        vku::unsafeProxy(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eFragment,
            0, sizeof(PushConstant),
        }),
    } },
    pipeline { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                vku::Shader { shader::outline_frag, vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout,
            1)
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
            {},
            vku::unsafeProxy(vk::Format::eB8G8R8A8Srgb),
        },
    }.get() } { }