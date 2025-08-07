module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.OutlineRenderPipeline;

import std;
export import glm;
export import vku;

import vk_gltf_viewer.math.bit;
import vk_gltf_viewer.shader.outline_frag;
import vk_gltf_viewer.shader.screen_quad_vert;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class OutlineRenderPipeline {
    public:
        struct PushConstant {
            glm::vec4 outlineColor;
            float outlineThickness;
        };

        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage>;

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        OutlineRenderPipeline(const vk::raii::Device &device LIFETIMEBOUND, std::uint32_t viewCount);

        void recreatePipeline(const vk::raii::Device &device, std::uint32_t viewCount);

    private:
        [[nodiscard]] vk::raii::Pipeline createPipeline(const vk::raii::Device &device, std::uint32_t viewCount) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pipeline::OutlineRenderPipeline::OutlineRenderPipeline(
    const vk::raii::Device &device,
    std::uint32_t viewCount
) : descriptorSetLayout {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy(DescriptorSetLayout::getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
        },
    },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo{
        {},
        *descriptorSetLayout,
        vku::unsafeProxy(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eFragment,
            0, sizeof(PushConstant),
        }),
    } },
    pipeline { createPipeline(device, viewCount) } { }

void vk_gltf_viewer::vulkan::OutlineRenderPipeline::recreatePipeline(
    const vk::raii::Device &device,
    std::uint32_t viewCount
) {
    pipeline = createPipeline(device, viewCount);
}

vk::raii::Pipeline vk_gltf_viewer::vulkan::OutlineRenderPipeline::createPipeline(
    const vk::raii::Device &device,
    std::uint32_t viewCount
) const {
    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                vku::Shader { shader::outline_frag, vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout,
            1)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eNone, {},
                {}, {}, {}, {},
                1.0f,
            }))
            .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                {},
                false, {},
                vku::unsafeProxy(vk::PipelineColorBlendAttachmentState {
                    true,
                    vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                    vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                }),
            { 1.f, 1.f, 1.f, 1.f },
        })),
        vk::PipelineRenderingCreateInfo {
            math::bit::ones(viewCount),
            vku::unsafeProxy(vk::Format::eB8G8R8A8Srgb),
        },
    }.get() };
}
