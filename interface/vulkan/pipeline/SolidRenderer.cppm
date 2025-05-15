module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.SolidRenderer;

import std;
export import glm;
import vku;
import :shader.screen_quad_vert;
import :shader.solid_frag;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct SolidRenderer {
        struct PushConstant {
            glm::vec4 color;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SolidRenderer(
            const vk::raii::Device &device LIFETIMEBOUND,
            const rp::Scene &sceneRenderPass LIFETIMEBOUND
        ) : pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                {},
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eFragment,
                    0, sizeof(PushConstant),
                }),
            } },
            pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::solid_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                *pipelineLayout, 1)
                .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                    {},
                    false, false,
                    vk::PolygonMode::eFill,
                    vk::CullModeFlagBits::eNone, {},
                    {}, {}, {}, {},
                    1.f,
                }))
                .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                    {},
                    false, {},
                    vku::unsafeProxy(vk::PipelineColorBlendAttachmentState {
                        true,
                        // Inverse alpha blending (src and dst are swapped) with premultiplied alpha
                        vk::BlendFactor::eOneMinusDstAlpha, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                        vk::BlendFactor::eOneMinusDstAlpha, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                    }),
                }))
                .setRenderPass(*sceneRenderPass)
                .setSubpass(2),
            } { }
    };
}