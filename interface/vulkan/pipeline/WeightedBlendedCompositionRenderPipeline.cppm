module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.WeightedBlendedCompositionRenderPipeline;

import std;
export import vku;

import vk_gltf_viewer.shader.screen_quad_vert;
import vk_gltf_viewer.shader.weighted_blended_composition_frag;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class WeightedBlendedCompositionRenderPipeline {
    public:
        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eInputAttachment>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        WeightedBlendedCompositionRenderPipeline(const Gpu &gpu LIFETIMEBOUND, const rp::Scene &renderPass LIFETIMEBOUND);

        void recreatePipeline(const Gpu &gpu, const rp::Scene &renderPass);

    private:
        [[nodiscard]] vk::raii::Pipeline createPipeline(const Gpu &gpu, const rp::Scene &renderPass) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::WeightedBlendedCompositionRenderPipeline::WeightedBlendedCompositionRenderPipeline(
    const Gpu &gpu,
    const rp::Scene &renderPass
) : descriptorSetLayout {
        gpu.device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy(DescriptorSetLayout::getBindings(
                { 1, vk::ShaderStageFlagBits::eFragment },
                { 1, vk::ShaderStageFlagBits::eFragment })),
        }
    },
    pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout
    } },
    pipeline { createPipeline(gpu, renderPass) } { }

void vk_gltf_viewer::vulkan::WeightedBlendedCompositionRenderPipeline::recreatePipeline(
    const Gpu &gpu,
    const rp::Scene &renderPass
) {
    pipeline = createPipeline(gpu, renderPass);
}

vk::raii::Pipeline vk_gltf_viewer::vulkan::WeightedBlendedCompositionRenderPipeline::createPipeline(
    const Gpu &gpu,
    const rp::Scene &renderPass
) const {
    return { gpu.device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
        createPipelineStages(
            gpu.device,
            vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
            vku::Shader {
                gpu.supportShaderTrinaryMinMax
                    ? std::span<const std::uint32_t> { shader::weighted_blended_composition_frag<1> }
                    : std::span<const std::uint32_t> { shader::weighted_blended_composition_frag<0> },
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
        .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
            {},
            false, {},
            vku::unsafeProxy(vk::PipelineColorBlendAttachmentState {
                true,
                vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
            }),
            { 1.f, 1.f, 1.f, 1.f },
        }))
        .setRenderPass(*renderPass)
        .setSubpass(2)
    };
}