module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.SkyboxRenderPipeline;

import std;
export import glm;
export import vku;

import vk_gltf_viewer.shader.skybox_frag;
import vk_gltf_viewer.shader.skybox_vert;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.Renderer;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.Skybox;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class SkyboxRenderPipeline {
    public:
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SkyboxRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            std::pair<const dsl::Renderer&, const dsl::Skybox&> descriptorSetLayouts LIFETIMEBOUND,
            const rp::Scene &renderPass LIFETIMEBOUND
        );

        void recreatePipeline(const vk::raii::Device &device, const rp::Scene &renderPass);

    private:
        [[nodiscard]] vk::raii::Pipeline createPipeline(const vk::raii::Device &device, const rp::Scene &renderPass) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::SkyboxRenderPipeline::SkyboxRenderPipeline(
    const vk::raii::Device &device,
    std::pair<const dsl::Renderer&, const dsl::Skybox&> descriptorSetLayouts,
    const rp::Scene &renderPass
) : pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy({ *descriptorSetLayouts.first, *descriptorSetLayouts.second }),
    } },
    pipeline { createPipeline(device, renderPass) }  { }

void vk_gltf_viewer::vulkan::pipeline::SkyboxRenderPipeline::recreatePipeline(
    const vk::raii::Device &device,
    const rp::Scene &renderPass
) {
    pipeline = createPipeline(device, renderPass);
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