module;

#include <cassert>
#include <cstddef>

export module vk_gltf_viewer:vulkan.pipeline.UnlitPrimitiveRenderer;

import std;
export import fastgltf;
import vku;
import :helpers.ranges;
import :shader_selector.unlit_primitive_frag;
import :shader_selector.unlit_primitive_vert;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    [[nodiscard]] vk::raii::Pipeline createUnlitPrimitiveRenderer(
        const vk::raii::Device &device,
        const pl::Primitive &layout,
        const rp::Scene &sceneRenderPass,
        const std::optional<fastgltf::ComponentType> &baseColorTexcoordComponentType,
        const std::optional<std::uint8_t> &colorComponentCount,
        fastgltf::AlphaMode alphaMode
    ) {
        struct VertexShaderSpecializationData {
            std::uint32_t texcoordComponentType = 5126; // FLOAT
            std::uint8_t colorComponentCount = 0;
        } vertexShaderSpecializationData{};

        if (baseColorTexcoordComponentType) {
            vertexShaderSpecializationData.texcoordComponentType = getGLComponentType(*baseColorTexcoordComponentType);
        }

        if (colorComponentCount) {
            assert(ranges::one_of(*colorComponentCount, 3, 4));
            vertexShaderSpecializationData.colorComponentCount = *colorComponentCount;
        }

        static constexpr std::array vertexShaderSpecializationMapEntries {
            vk::SpecializationMapEntry {
                0,
                offsetof(VertexShaderSpecializationData, texcoordComponentType),
                sizeof(VertexShaderSpecializationData::texcoordComponentType),
             },
            vk::SpecializationMapEntry {
                1,
                offsetof(VertexShaderSpecializationData, colorComponentCount),
                sizeof(VertexShaderSpecializationData::colorComponentCount),
            },
        };

        const vk::SpecializationInfo vertexShaderSpecializationInfo {
            vertexShaderSpecializationMapEntries,
            vk::ArrayProxyNoTemporaries<const VertexShaderSpecializationData> { vertexShaderSpecializationData },
        };

        const vku::RefHolder pipelineStages = createPipelineStages(
            device,
            vku::Shader {
                shader_selector::unlit_primitive_vert(baseColorTexcoordComponentType.has_value(), colorComponentCount.has_value()),
                vk::ShaderStageFlagBits::eVertex,
                &vertexShaderSpecializationInfo,
            },
            vku::Shader {
                shader_selector::unlit_primitive_frag(baseColorTexcoordComponentType.has_value(), colorComponentCount.has_value(), alphaMode),
                vk::ShaderStageFlagBits::eFragment,
            });

        switch (alphaMode) {
            case fastgltf::AlphaMode::Opaque:
                return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                    pipelineStages.get(), *layout, 1, true, vk::SampleCountFlagBits::e4)
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
                    pipelineStages.get(), *layout, 1, true, vk::SampleCountFlagBits::e4)
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
                    pipelineStages.get(), *layout, 1, true, vk::SampleCountFlagBits::e4)
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