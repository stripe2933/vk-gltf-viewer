module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pipeline.SkyboxRenderer;

import std;
export import glm;
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
            const vk::raii::Device &device [[clang::lifetimebound]],
            const dsl::Skybox &descriptorSetLayout [[clang::lifetimebound]],
            bool isCubemapImageToneMapped,
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]],
            const buffer::CubeIndices &cubeIndices [[clang::lifetimebound]]
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
                    vku::Shader { COMPILED_SHADER_DIR "/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex },
                    vku::Shader {
                        COMPILED_SHADER_DIR "/skybox.frag.spv",
                        vk::ShaderStageFlagBits::eFragment,
                        vk::SpecializationInfo {
                            vku::unsafeProxy(vk::SpecializationMapEntry { 0, 0, sizeof(vk::Bool32) }),
                            vku::unsafeProxy<vk::Bool32>(isCubemapImageToneMapped),
                        },
                    }).get(),
                *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                    {},
                    false, false,
                    vk::PolygonMode::eFill,
                    vk::CullModeFlagBits::eNone, {},
                    {}, {}, {}, {},
                    1.f,
                }))
                .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                    {},
                    true, false, vk::CompareOp::eEqual,
                }))
                .setRenderPass(*sceneRenderPass)
                .setSubpass(0),
            },
            cubeIndices { cubeIndices } { }

        auto draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<dsl::Skybox> descriptorSet, const PushConstant &pushConstant) const -> void {
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