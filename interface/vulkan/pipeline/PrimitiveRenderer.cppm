export module vk_gltf_viewer:vulkan.pipeline.PrimitiveRenderer;

import std;
export import fastgltf;
import vku;
import :shader.primitive_vert;
import :shader.primitive_frag;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export
    [[nodiscard]] vk::raii::Pipeline createPrimitiveRenderer(
        const vk::raii::Device &device,
        const pl::Primitive &pipelineLayout,
        const rp::Scene &sceneRenderPass,
        bool fragmentShaderGeneratedTBN,
        fastgltf::AlphaMode alphaMode
    ) {
        switch (alphaMode) {
            case fastgltf::AlphaMode::Opaque:
                return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader {
                            fragmentShaderGeneratedTBN
                                ? std::span<const std::uint32_t> { shader::primitive_vert<1> }
                                : std::span<const std::uint32_t> { shader::primitive_vert<0> },
                            vk::ShaderStageFlagBits::eVertex,
                        },
                        vku::Shader {
                            fragmentShaderGeneratedTBN
                                ? std::span<const std::uint32_t> { shader::primitive_frag<1, 0> }
                                : std::span<const std::uint32_t> { shader::primitive_frag<0, 0> },
                            vk::ShaderStageFlagBits::eFragment,
                        }).get(),
                    *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                    .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                        {},
                        true, true, vk::CompareOp::eGreater, // Use reverse Z.
                    }))
                    .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                        {},
                        vku::unsafeProxy({
                            vk::DynamicState::eViewport,
                            vk::DynamicState::eScissor,
                            vk::DynamicState::eCullMode,
                        }),
                    }))
                    .setRenderPass(*sceneRenderPass)
                    .setSubpass(0)
                };
            case fastgltf::AlphaMode::Mask:
                return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader {
                            fragmentShaderGeneratedTBN
                                ? std::span<const std::uint32_t> { shader::primitive_vert<1> }
                                : std::span<const std::uint32_t> { shader::primitive_vert<0> },
                            vk::ShaderStageFlagBits::eVertex,
                        },
                        vku::Shader {
                            fragmentShaderGeneratedTBN
                                ? std::span<const std::uint32_t> { shader::primitive_frag<1, 1> }
                                : std::span<const std::uint32_t> { shader::primitive_frag<0, 1> },
                            vk::ShaderStageFlagBits::eFragment,
                        }).get(),
                    *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                    .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                        {},
                        true, true, vk::CompareOp::eGreater, // Use reverse Z.
                    }))
                    .setPMultisampleState(vku::unsafeAddress(vk::PipelineMultisampleStateCreateInfo {
                        {},
                        vk::SampleCountFlagBits::e4,
                        {}, {}, {},
                        true,
                    }))
                    .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                        {},
                        vku::unsafeProxy({
                            vk::DynamicState::eViewport,
                            vk::DynamicState::eScissor,
                            vk::DynamicState::eCullMode,
                        }),
                    }))
                    .setRenderPass(*sceneRenderPass)
                    .setSubpass(0)
                };
            case fastgltf::AlphaMode::Blend:
                return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader {
                            fragmentShaderGeneratedTBN
                                ? std::span<const std::uint32_t> { shader::primitive_vert<1> }
                                : std::span<const std::uint32_t> { shader::primitive_vert<0> },
                            vk::ShaderStageFlagBits::eVertex,
                        },
                        vku::Shader {
                            fragmentShaderGeneratedTBN
                                ? std::span<const std::uint32_t> { shader::primitive_frag<1, 2> }
                                : std::span<const std::uint32_t> { shader::primitive_frag<0, 2> },
                            vk::ShaderStageFlagBits::eFragment,
                        }).get(),
                    *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                    .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                        {},
                        false, false,
                        vk::PolygonMode::eFill,
                        // Translucent objects' back faces shouldn't be culled.
                        vk::CullModeFlagBits::eNone, {},
                        false, {}, {}, {},
                        1.f,
                    }))
                    .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                        {},
                        // Translucent objects shouldn't interfere with the pre-rendered depth buffer. Use reverse Z.
                        true, false, vk::CompareOp::eGreater,
                    }))
                    .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                        {},
                        false, {},
                        vku::unsafeProxy({
                            vk::PipelineColorBlendAttachmentState {
                                true,
                                vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                                vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                            },
                            vk::PipelineColorBlendAttachmentState {
                                true,
                                vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcColor, vk::BlendOp::eAdd,
                                vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                                vk::ColorComponentFlagBits::eR,
                            },
                        }),
                        { 1.f, 1.f, 1.f, 1.f },
                    }))
                    .setRenderPass(*sceneRenderPass)
                    .setSubpass(1)
                };
        }
        std::unreachable();
    }
}