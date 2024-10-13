export module vk_gltf_viewer:vulkan.pipeline.CubemapToneMappingRenderer;

import std;
export import glm;
export import :vulkan.rp.CubemapToneMapping;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class CubemapToneMappingRenderer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> {
            explicit DescriptorSetLayout(const vk::raii::Device &device [[clang::lifetimebound]])
                : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> { device, vk::DescriptorSetLayoutCreateInfo {
                    {},
                    vku::unsafeProxy(vk::DescriptorSetLayoutBinding {
                        0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eFragment,
                    }),
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
                    vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/screen_quad.vert.spv", vk::ShaderStageFlagBits::eVertex),
                    vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/cubemap_tone_mapping.frag.spv", vk::ShaderStageFlagBits::eFragment)).get(),
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