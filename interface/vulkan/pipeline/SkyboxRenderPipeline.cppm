module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.SkyboxRenderPipeline;

import std;
export import glm;
export import vku;

import vk_gltf_viewer.shader.skybox_frag;
import vk_gltf_viewer.shader.skybox_vert;
export import vk_gltf_viewer.vulkan.buffer.CubeIndices;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class SkyboxRenderPipeline {
        vk::raii::Sampler cubemapSampler;
        
    public:
        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler>;

        struct PushConstant {
            glm::mat4 projectionView;
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SkyboxRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const rp::Scene &renderPass LIFETIMEBOUND,
            const buffer::CubeIndices &cubeIndexBuffer LIFETIMEBOUND
        );

        void recreatePipeline(const vk::raii::Device &device, const rp::Scene &renderPass);

        void draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<DescriptorSetLayout> descriptorSet, const PushConstant &pushConstant) const;

    private:
        std::reference_wrapper<const buffer::CubeIndices> cubeIndexBuffer;

        [[nodiscard]] vk::raii::Pipeline createPipeline(const vk::raii::Device &device, const rp::Scene &renderPass) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::SkyboxRenderPipeline::SkyboxRenderPipeline(
    const vk::raii::Device &device,
    const rp::Scene &renderPass,
    const buffer::CubeIndices &cubeIndexBuffer
) : cubemapSampler { device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
    }.setMaxLod(vk::LodClampNone) },
    descriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(DescriptorSetLayout::getBindings({ 1, vk::ShaderStageFlagBits::eFragment, &*cubemapSampler })),
    } },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        vku::unsafeProxy(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eVertex,
            0, sizeof(PushConstant),
        }),
    } },
    pipeline { createPipeline(device, renderPass) },
    cubeIndexBuffer { cubeIndexBuffer } { }

void vk_gltf_viewer::vulkan::pipeline::SkyboxRenderPipeline::recreatePipeline(
    const vk::raii::Device &device,
    const rp::Scene &renderPass
) {
    pipeline = createPipeline(device, renderPass);
}

void vk_gltf_viewer::vulkan::pipeline::SkyboxRenderPipeline::draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<DescriptorSetLayout> descriptorSet, const PushConstant &pushConstant) const {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSet, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
    commandBuffer.bindIndexBuffer(cubeIndexBuffer.get(), 0, vk::IndexType::eUint16);
    commandBuffer.drawIndexed(36, 1, 0, 0, 0);
}

vk::raii::Pipeline vk_gltf_viewer::vulkan::pipeline::SkyboxRenderPipeline::createPipeline(
    const vk::raii::Device &device,
    const rp::Scene &renderPass
) const {
    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
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
        .setRenderPass(*renderPass)
        .setSubpass(0),
    };
}