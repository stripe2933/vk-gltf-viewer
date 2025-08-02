module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.BloomApplyRenderPipeline;

import std;

import vk_gltf_viewer.shader.bloom_apply_frag;
import vk_gltf_viewer.shader.screen_quad_vert;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.render_pass.BloomApply;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class BloomApplyRenderPipeline {
    public:
        struct PushConstant {
            float factor;
        };

        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eInputAttachment>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        BloomApplyRenderPipeline(
            const Gpu &gpu LIFETIMEBOUND,
            const rp::BloomApply &renderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::BloomApplyRenderPipeline::BloomApplyRenderPipeline(
    const Gpu &gpu,
    const rp::BloomApply &renderPass
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