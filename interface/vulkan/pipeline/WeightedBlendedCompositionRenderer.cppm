export module vk_gltf_viewer:vulkan.pipeline.WeightedBlendedCompositionRenderer;

import vku;
export import vulkan_hpp;
export import :vulkan.rp.Scene;
export import :vulkan.shader.ScreenQuadVertex;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct WeightedBlendedCompositionRenderer {
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eInputAttachment> {
            explicit DescriptorSetLayout(const vk::raii::Device &device [[clang::lifetimebound]])
                : vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eInputAttachment> {
                    device,
                    vk::DescriptorSetLayoutCreateInfo {
                        {},
                        vku::unsafeProxy({
                            vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eInputAttachment, 1, vk::ShaderStageFlagBits::eFragment },
                            vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eInputAttachment, 1, vk::ShaderStageFlagBits::eFragment },
                        }),
                    }
                } { }
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        WeightedBlendedCompositionRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const shader::ScreenQuadVertex &vertexShader,
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : descriptorSetLayout { device },
            pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout
            } },
            pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vertexShader,
                    vku::Shader { COMPILED_SHADER_DIR "/weighted_blended_composition.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
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