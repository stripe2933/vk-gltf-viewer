module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.SkyboxRenderer;

import std;
export import glm;
import :shader.skybox_frag;
import :shader.skybox_vert;
export import :vulkan.buffer.CubeIndices;
export import :vulkan.dsl.Skybox;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class SkyboxRenderer {
    public:
        struct PushConstant {
            glm::mat4 projectionView;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SkyboxRenderer(
            const vk::raii::Device &device LIFETIMEBOUND,
            const dsl::Skybox &descriptorSetLayout LIFETIMEBOUND,
            const rp::Scene &sceneRenderPass LIFETIMEBOUND,
            const buffer::CubeIndices &cubeIndices LIFETIMEBOUND
        ) : pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eVertex,
                    0, sizeof(PushConstant),
                }),
            } },
            pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::skybox_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::skybox_frag, vk::ShaderStageFlagBits::eFragment }).get(),
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
                        // Inverse alpha blending (src and dst are swapped) with premultiplied alpha
                        vk::BlendFactor::eOneMinusDstAlpha, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                        vk::BlendFactor::eOneMinusDstAlpha, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                    }),
                }))
                .setRenderPass(*sceneRenderPass)
                .setSubpass(2),
            },
            cubeIndices { cubeIndices } { }

        void draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<dsl::Skybox> descriptorSet, const PushConstant &pushConstant) const {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSet, {});
            commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
            commandBuffer.bindIndexBuffer(cubeIndices, 0, vk::IndexType::eUint16);
            commandBuffer.drawIndexed(36, 1, 0, 0, 0);
        }

    private:
        const buffer::CubeIndices &cubeIndices;
    };
}