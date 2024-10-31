module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pipeline.DepthRenderer;

import std;
export import glm;
import vku;
export import :vulkan.dsl.Asset;
export import :vulkan.dsl.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct DepthRenderer {
        struct PushConstant {
            glm::mat4 projectionView;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        DepthRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const std::pair<const dsl::Asset&, const dsl::Scene&> &descriptorSetLayouts
        ) : pipelineLayout { device, vk::PipelineLayoutCreateInfo{
                {},
                vku::unsafeProxy(std::apply([](const auto &...x) { return std::array { *x... }; }, descriptorSetLayouts)),
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eVertex,
                    0, sizeof(PushConstant),
                }),
            } },
            pipeline { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/depth.vert.spv", vk::ShaderStageFlagBits::eVertex),
                        vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/depth.frag.spv", vk::ShaderStageFlagBits::eFragment)).get(),
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
                    vku::unsafeProxy(vk::Format::eR32Uint),
                    vk::Format::eD32Sfloat,
                }
            }.get() } { }

        void pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const {
            commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
        }
    };
}