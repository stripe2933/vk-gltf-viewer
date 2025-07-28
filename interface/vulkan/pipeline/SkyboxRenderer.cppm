module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.SkyboxRenderer;

import std;

import vk_gltf_viewer.shader.skybox_frag;
import vk_gltf_viewer.shader.skybox_vert;
export import vk_gltf_viewer.vulkan.sampler.Cubemap;
export import vk_gltf_viewer.vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct SkyboxRenderer {
        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eCombinedImageSampler>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SkyboxRenderer(
            const vk::raii::Device &device LIFETIMEBOUND,
            const sampler::Cubemap &cubemapSampler LIFETIMEBOUND,
            const rp::Scene &sceneRenderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::SkyboxRenderer::SkyboxRenderer(
    const vk::raii::Device &device,
    const sampler::Cubemap &cubemapSampler,
    const rp::Scene &sceneRenderPass
) : descriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(DescriptorSetLayout::getBindings(
            { 1, vk::ShaderStageFlagBits::eVertex },
            { 1, vk::ShaderStageFlagBits::eFragment, &*cubemapSampler })),
    } },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
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
    } { }