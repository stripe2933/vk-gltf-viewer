export module vk_gltf_viewer:vulkan.pipeline.AlphaMaskedJumpFloodSeedRenderer;

import std;
export import glm;
export import vku;
export import :vulkan.dsl.Asset;
export import :vulkan.dsl.Scene;

namespace vk_gltf_viewer::vulkan::pipeline {
    export struct AlphaMaskedJumpFloodSeedRenderer {
        struct PushConstant {
            glm::mat4 projectionView;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        AlphaMaskedJumpFloodSeedRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            std::tuple<const dsl::Scene&, const dsl::Asset&> descriptorSetLayouts [[clang::lifetimebound]]
        ) : pipelineLayout { device, vk::PipelineLayoutCreateInfo{
                {},
                vku::unsafeProxy(std::apply([](const auto &...x) { return std::array { *x... }; }, descriptorSetLayouts)),
                vku::unsafeProxy({
                    vk::PushConstantRange {
                        vk::ShaderStageFlagBits::eVertex,
                        0, sizeof(PushConstant),
                    },
                }),
            } },
            pipeline { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader { COMPILED_SHADER_DIR "/alpha_masked_jump_flood_seed.vert.spv", vk::ShaderStageFlagBits::eVertex },
                        vku::Shader { COMPILED_SHADER_DIR "/alpha_masked_jump_flood_seed.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
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
                    vku::unsafeProxy({ vk::Format::eR16G16Uint }),
                    vk::Format::eD32Sfloat,
                }
            }.get() } { }

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
        }

        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void {
            commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
        }
    };
}