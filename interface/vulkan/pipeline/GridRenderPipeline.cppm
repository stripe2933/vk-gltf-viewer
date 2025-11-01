module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.GridRenderPipeline;

import std;
export import glm;
import vku;
export import vulkan_hpp;

import vk_gltf_viewer.shader.grid_vert;
import vk_gltf_viewer.shader.grid_frag;
import vk_gltf_viewer.vulkan.descriptor_set_layout.Renderer;
import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct GridRenderPipeline {
        struct PushConstant {
            static constexpr vk::PushConstantRange range = {
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, 20,
            };

            glm::vec3 color;
            vk::Bool32 showMinorAxes;
            float size;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        GridRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const dsl::Renderer &descriptorSetLayout LIFETIMEBOUND,
            const rp::Scene &renderPass LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::GridRenderPipeline::GridRenderPipeline(
    const vk::raii::Device &device,
    const dsl::Renderer &descriptorSetLayout,
    const rp::Scene &renderPass
) : pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        PushConstant::range,
    } },
    pipeline { device, nullptr, vk::GraphicsPipelineCreateInfo {
        {},
        vku::lvalue({
            vk::PipelineShaderStageCreateInfo {
                {},
                vk::ShaderStageFlagBits::eVertex,
                *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                    {},
                    shader::grid_vert,
                } }),
                "main",
            },
            vk::PipelineShaderStageCreateInfo {
                {},
                vk::ShaderStageFlagBits::eFragment,
                *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                    {},
                    shader::grid_frag,
                } }),
                "main",
            },
        }),
        &vku::lvalue(vk::PipelineVertexInputStateCreateInfo{}),
        &vku::lvalue(vku::defaultPipelineInputAssemblyState(vk::PrimitiveTopology::eTriangleList)),
        nullptr,
        &vku::lvalue(vk::PipelineViewportStateCreateInfo{}),
        &vku::lvalue(vk::PipelineRasterizationStateCreateInfo {
            {},
            true /* depth clipping should be disabled */, false,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone, {},
            false, {}, {}, {},
            1.f,
        }),
        &vku::lvalue(vk::PipelineMultisampleStateCreateInfo {
            {}, vk::SampleCountFlagBits::e4,
            false, {}, {},
            true,
        }),
        &vku::lvalue(vk::PipelineDepthStencilStateCreateInfo {
            {},
            true, true, vk::CompareOp::eGreaterOrEqual,
        }),
        &vku::lvalue(vk::PipelineColorBlendStateCreateInfo {
            {},
            false, {},
            vku::lvalue(vk::PipelineColorBlendAttachmentState {
                true,
                vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
            }),
            { 1.f, 1.f, 1.f, 1.f },
        }),
        &vku::lvalue(vk::PipelineDynamicStateCreateInfo {
            {},
            vku::lvalue({ vk::DynamicState::eViewportWithCount, vk::DynamicState::eScissorWithCount }),
        }),
        *pipelineLayout,
        *renderPass, 0,
    } } { }
