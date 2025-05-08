module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.WeightedBlendedCompositionRenderer;

import vku;
import vk_gltf_viewer.shader.screen_quad_vert;
import vk_gltf_viewer.shader.weighted_blended_composition_frag;
export import vulkan_hpp;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct WeightedBlendedCompositionRenderer {
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eInputAttachment> {
            explicit DescriptorSetLayout(const vk::raii::Device &device LIFETIMEBOUND)
                : vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eInputAttachment> {
                    device,
                    vk::DescriptorSetLayoutCreateInfo {
                        {},
                        vku::unsafeProxy(getBindings(
                            { 1, vk::ShaderStageFlagBits::eFragment },
                            { 1, vk::ShaderStageFlagBits::eFragment })),
                    }
                } { }
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        WeightedBlendedCompositionRenderer(
            const vk::raii::Device &device LIFETIMEBOUND,
            const rp::Scene &sceneRenderPass LIFETIMEBOUND
        ) : descriptorSetLayout { device },
            pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout
            } },
            pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::weighted_blended_composition_frag, vk::ShaderStageFlagBits::eFragment }).get(),
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
                        vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                        vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                    }),
                    { 1.f, 1.f, 1.f, 1.f },
                }))
                .setRenderPass(*sceneRenderPass)
                .setSubpass(2)
            } { }
    };
}