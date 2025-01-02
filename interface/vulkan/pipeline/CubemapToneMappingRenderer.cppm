export module vk_gltf_viewer:vulkan.pipeline.CubemapToneMappingRenderer;

import std;
export import glm;
export import vku;
import :shader.screen_quad_vert;
import :shader.cubemap_tone_mapping_frag;
export import :vulkan.rp.CubemapToneMapping;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class CubemapToneMappingRenderer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> {
            explicit DescriptorSetLayout(const vk::raii::Device &device [[clang::lifetimebound]])
                : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> { device, vk::DescriptorSetLayoutCreateInfo {
                    {},
                    vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
                } } { }
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        CubemapToneMappingRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const rp::CubemapToneMapping &renderPass [[clang::lifetimebound]]
        ) : descriptorSetLayout { device },
            pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
            } },
            pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::cubemap_tone_mapping_frag, vk::ShaderStageFlagBits::eFragment }).get(),
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