module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.SkyboxRenderPipeline;

import std;
export import glm;

import vk_gltf_viewer.shader.skybox_frag;
import vk_gltf_viewer.shader.skybox_vert;
export import vk_gltf_viewer.vulkan.buffer.CubeIndices;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.Skybox;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class SkyboxRenderPipeline {
    public:
        struct PushConstant {
            glm::mat4 projectionView;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SkyboxRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const dsl::Skybox &descriptorSetLayout LIFETIMEBOUND,
            const rp::Scene &sceneRenderPass LIFETIMEBOUND,
            const buffer::CubeIndices &cubeIndices LIFETIMEBOUND
        );

        void draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<dsl::Skybox> descriptorSet, const PushConstant &pushConstant) const;

    private:
        const buffer::CubeIndices &cubeIndices;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::SkyboxRenderPipeline::SkyboxRenderPipeline(
    const vk::raii::Device &device,
    const dsl::Skybox &descriptorSetLayout,
    const rp::Scene &sceneRenderPass,
    const buffer::CubeIndices &cubeIndices
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

void vk_gltf_viewer::vulkan::pipeline::SkyboxRenderPipeline::draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<dsl::Skybox> descriptorSet, const PushConstant &pushConstant) const {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSet, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
    commandBuffer.bindIndexBuffer(cubeIndices, 0, vk::IndexType::eUint16);
    commandBuffer.drawIndexed(36, 1, 0, 0, 0);
}