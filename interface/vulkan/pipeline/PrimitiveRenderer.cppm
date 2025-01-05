module;

#include <cassert>

export module vk_gltf_viewer:vulkan.pipeline.PrimitiveRenderer;

import std;
export import fastgltf;
import vku;
import :helpers.ranges;
import :shader_selector.primitive_vert;
import :shader_selector.primitive_frag;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    [[nodiscard]] vk::raii::Pipeline createPrimitiveRenderer(
        const vk::raii::Device &device,
        const pl::Primitive &pipelineLayout,
        const rp::Scene &sceneRenderPass,
        std::uint32_t texcoordCount,
        const std::optional<std::uint8_t> &colorComponentCount,
        bool fragmentShaderGeneratedTBN,
        fastgltf::AlphaMode alphaMode
    ) {
        static constexpr std::array vertexShaderSpecializationMapEntries {
            vk::SpecializationMapEntry { 0, 0, sizeof(std::uint32_t) },
        };

        std::array vertexShaderSpecializationData { 0U };
        if (colorComponentCount) {
            assert(ranges::one_of(*colorComponentCount, 3, 4));
            get<0>(vertexShaderSpecializationData) = *colorComponentCount;
        }

        const vk::SpecializationInfo vertexShaderSpecializationInfo {
            vertexShaderSpecializationMapEntries,
            vk::ArrayProxyNoTemporaries<const std::uint32_t> { vertexShaderSpecializationData },
        };

        const vku::RefHolder pipelineStages = createPipelineStages(
            device,
            vku::Shader {
                shader_selector::primitive_vert(texcoordCount, colorComponentCount.has_value(), fragmentShaderGeneratedTBN),
                vk::ShaderStageFlagBits::eVertex,
                &vertexShaderSpecializationInfo,
            },
            vku::Shader {
                shader_selector::primitive_frag(texcoordCount, colorComponentCount.has_value(), fragmentShaderGeneratedTBN, alphaMode),
                vk::ShaderStageFlagBits::eFragment,
            });

        switch (alphaMode) {
            case fastgltf::AlphaMode::Opaque:
                return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                    pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
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
                    pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
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
                    pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
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