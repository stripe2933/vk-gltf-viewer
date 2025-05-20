module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.InverseToneMappingRenderer;

import std;
import :shader.screen_quad_vert;
import :shader.inverse_tone_mapping_frag;
export import :vulkan.Gpu;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct InverseToneMappingRenderer {
        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        InverseToneMappingRenderer(const Gpu &gpu LIFETIMEBOUND, const rp::Scene &renderPass LIFETIMEBOUND)
            : descriptorSetLayout { gpu.device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::unsafeProxy(DescriptorSetLayout::getBindings(
                    { 1, vk::ShaderStageFlagBits::eFragment })),
            } }
            , pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
            } }
            , pipeline { gpu.device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    gpu.device,
                    vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader {
                        gpu.supportShaderTrinaryMinMax
                            ? std::span<const std::uint32_t> { shader::inverse_tone_mapping_frag<1> }
                            : std::span<const std::uint32_t> { shader::inverse_tone_mapping_frag<0> },
                        vk::ShaderStageFlagBits::eFragment,
                    }).get(),
                *pipelineLayout, 1, true)
                .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                    {},
                    false, false,
                    vk::PolygonMode::eFill,
                    vk::CullModeFlagBits::eNone, {},
                    {}, {}, {}, {},
                    1.f,
                }))
                // Only stencil test passed fragment's (which indicates the fragment's corresponding material's emissive
                // strength > 1.0) subpass input will be inverse tone mapped.
                .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                    {},
                    false, false, {}, false,
                    true,
                    vk::StencilOpState { vk::StencilOp::eKeep, vk::StencilOp::eKeep, {}, vk::CompareOp::eEqual, ~0U, {}, 1U },
                    vk::StencilOpState { vk::StencilOp::eKeep, vk::StencilOp::eKeep, {}, vk::CompareOp::eEqual, ~0U, {}, 1U },
                }))
                .setRenderPass(*renderPass)
                .setSubpass(3),
            } { }
    };
}