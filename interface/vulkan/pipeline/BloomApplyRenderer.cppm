module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.BloomApplyRenderer;

import std;
import :shader.screen_quad_vert;
import :shader.bloom_apply_frag;
export import :vulkan.Gpu;
export import :vulkan.rp.BloomApply;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class BloomApplyRenderer {
    public:
        struct PushConstant {
            float factor;
        };

        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eInputAttachment>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        BloomApplyRenderer(
            const Gpu &gpu LIFETIMEBOUND,
            const rp::BloomApply &renderPass LIFETIMEBOUND
        ) : descriptorSetLayout { gpu.device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::unsafeProxy(DescriptorSetLayout::getBindings(
                    { 1, vk::ShaderStageFlagBits::eFragment },
                    { 1, vk::ShaderStageFlagBits::eFragment })),
            } },
            pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eFragment,
                    0, sizeof(PushConstant),
                }),
            } },
            pipeline { gpu.device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    gpu.device,
                    vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader {
                        gpu.supportShaderTrinaryMinMax
                            ? std::span<const std::uint32_t> { shader::bloom_apply_frag<1> }
                            : std::span<const std::uint32_t> { shader::bloom_apply_frag<0> },
                        vk::ShaderStageFlagBits::eFragment,
                    }).get(),
                *pipelineLayout, 1)
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
    };
}