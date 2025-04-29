module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.MousePickingRenderer;

export import vku;
export import :vulkan.rp.MousePicking;
import :shader.screen_quad_vert;
import :shader.mouse_picking_frag;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct MousePickingRenderer {
        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eStorageBuffer>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        MousePickingRenderer(
            const vk::raii::Device &device LIFETIMEBOUND,
            const rp::MousePicking &renderPass LIFETIMEBOUND
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
    };
}