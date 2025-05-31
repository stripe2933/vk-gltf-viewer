module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.CubemapToneMappingRenderer;

import std;
export import glm;
import :shader.screen_quad_vert;
import :shader.cubemap_tone_mapping_frag;

export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.rp.CubemapToneMapping;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class CubemapToneMappingRenderer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> {
            explicit DescriptorSetLayout(const vk::raii::Device &device LIFETIMEBOUND)
                : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> { device, vk::DescriptorSetLayoutCreateInfo {
                    vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                    vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
                } } { }
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        CubemapToneMappingRenderer(
            const Gpu &gpu LIFETIMEBOUND,
            const rp::CubemapToneMapping &renderPass LIFETIMEBOUND
        ) : descriptorSetLayout { gpu.device },
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
    };
}