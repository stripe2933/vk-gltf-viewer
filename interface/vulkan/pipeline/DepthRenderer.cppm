export module vk_gltf_viewer:vulkan.pipeline.DepthRenderer;

import vku;
import :shader.depth_vert;
import :shader.depth_frag;
import :shader.mask_depth_vert;
import :shader.mask_depth_frag;
export import :vulkan.pl.PrimitiveNoShading;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    [[nodiscard]] vk::raii::Pipeline createDepthRenderer(
        const vk::raii::Device &device,
        const pl::PrimitiveNoShading &pipelineLayout,
        bool mask
    ) {
        if (mask) {
            return { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader { shader::mask_depth_vert, vk::ShaderStageFlagBits::eVertex },
                        vku::Shader { shader::mask_depth_frag, vk::ShaderStageFlagBits::eFragment }).get(),
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
        else {
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
    }
}