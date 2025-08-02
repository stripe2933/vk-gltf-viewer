module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.CubemapToneMappingRenderPipeline;

import std;

import vk_gltf_viewer.shader.cubemap_tone_mapping_frag;
import vk_gltf_viewer.shader.screen_quad_vert;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.rp.CubemapToneMapping;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class CubemapToneMappingRenderPipeline {
    public:
        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        CubemapToneMappingRenderPipeline(
            const Gpu &gpu LIFETIMEBOUND,
            const rp::CubemapToneMapping &renderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::CubemapToneMappingRenderPipeline::CubemapToneMappingRenderPipeline(
    const Gpu &gpu,
    const rp::CubemapToneMapping &renderPass
) : descriptorSetLayout { gpu.device, vk::DescriptorSetLayoutCreateInfo {
        vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        vku::unsafeProxy(DescriptorSetLayout::getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
    } },
    pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
    } },
    pipeline { gpu.device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
        createPipelineStages(
            gpu.device,
            vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
            vku::Shader {
                gpu.supportShaderTrinaryMinMax
                    ? std::span<const std::uint32_t> { shader::cubemap_tone_mapping_frag<1> }
                    : std::span<const std::uint32_t> { shader::cubemap_tone_mapping_frag<0> },
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