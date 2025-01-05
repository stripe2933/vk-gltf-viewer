module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.pipeline.DepthRenderer;

import std;
import vku;
import :shader.depth_vert;
import :shader.depth_frag;
import :shader_selector.mask_depth_vert;
import :shader_selector.mask_depth_frag;
export import :vulkan.pl.PrimitiveNoShading;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    [[nodiscard]] vk::raii::Pipeline createDepthRenderer(
        const vk::raii::Device &device,
        const pl::PrimitiveNoShading &pipelineLayout
    ) {
        return { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::depth_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::depth_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                *pipelineLayout, 1, true)
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
                })),
            vk::PipelineRenderingCreateInfo {
                {},
                vku::unsafeProxy(vk::Format::eR16Uint),
                vk::Format::eD32Sfloat,
            }
        }.get() };
    }

    [[nodiscard]] vk::raii::Pipeline createMaskDepthRenderer(
        const vk::raii::Device &device,
        const pl::PrimitiveNoShading &pipelineLayout,
        const std::optional<fastgltf::ComponentType> &baseColorTexcoordComponentType,
        bool hasColorAlphaAttribute
    ) {
        struct VertexShaderSpecializationData {
            std::uint32_t texcoordComponentType = 5126; // FLOAT
        } vertexShaderSpecializationData{};

        if (baseColorTexcoordComponentType) {
            vertexShaderSpecializationData.texcoordComponentType = getGLComponentType(*baseColorTexcoordComponentType);
        }

        return { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader {
                        shader_selector::mask_depth_vert(baseColorTexcoordComponentType.has_value(), hasColorAlphaAttribute),
                        vk::ShaderStageFlagBits::eVertex,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            vku::unsafeProxy(vk::SpecializationMapEntry { 0, offsetof(VertexShaderSpecializationData, texcoordComponentType), sizeof(VertexShaderSpecializationData::texcoordComponentType) }),
                            vk::ArrayProxyNoTemporaries<const VertexShaderSpecializationData> { vertexShaderSpecializationData },
                        }),
                    },
                    vku::Shader {
                        shader_selector::mask_depth_frag(baseColorTexcoordComponentType.has_value(), hasColorAlphaAttribute),
                        vk::ShaderStageFlagBits::eFragment,
                    }).get(),
                *pipelineLayout, 1, true)
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
                })),
            vk::PipelineRenderingCreateInfo {
                    {},
                    vku::unsafeProxy(vk::Format::eR16Uint),
                    vk::Format::eD32Sfloat,
                }
        }.get() };
    }
}