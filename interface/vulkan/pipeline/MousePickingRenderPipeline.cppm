module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.MousePickingRenderPipeline;

#ifdef _MSC_VER
import std;
#endif
export import vku;

import vk_gltf_viewer.shader.mouse_picking_frag;
import vk_gltf_viewer.shader.screen_quad_vert;
export import vk_gltf_viewer.vulkan.render_pass.MousePicking;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct MousePickingRenderPipeline {
        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eStorageBuffer>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        MousePickingRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const rp::MousePicking &renderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::MousePickingRenderPipeline::MousePickingRenderPipeline(
    const vk::raii::Device &device,
    const rp::MousePicking &renderPass
) : descriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(DescriptorSetLayout::getBindings(
            { 1, vk::ShaderStageFlagBits::eFragment },
            { 1, vk::ShaderStageFlagBits::eFragment })),
    } },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
    } },
    pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
        vku::createPipelineStages(
            device,
            vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
            vku::Shader { shader::mouse_picking_frag, vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout)
        .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
            {},
            false, false,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone, {},
            {}, {}, {}, {},
            1.f,
        }))
        .setRenderPass(*renderPass)
        .setSubpass(1),
    } { }