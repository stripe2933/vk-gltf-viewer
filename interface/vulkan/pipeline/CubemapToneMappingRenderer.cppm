module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.CubemapToneMappingRenderer;

import std;
export import vku;
import :shader.screen_quad_vert;
import :shader.cubemap_tone_mapping_frag;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class CubemapToneMappingRenderer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> {
            explicit DescriptorSetLayout(const vk::raii::Device &device LIFETIMEBOUND)
                : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> { device, vk::DescriptorSetLayoutCreateInfo {
                    vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                    vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
                } } { }
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit CubemapToneMappingRenderer(const vk::raii::Device &device LIFETIMEBOUND)
            : descriptorSetLayout { device }
            , pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
            } }
            , pipeline { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                        vku::Shader { shader::cubemap_tone_mapping_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                    *pipelineLayout, 1)
                    .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                        {},
                        false, false,
                        vk::PolygonMode::eFill,
                        vk::CullModeFlagBits::eNone, {},
                        {}, {}, {}, {},
                        1.f,
                    })),
                vk::PipelineRenderingCreateInfo {
                    0b111111U,
                    vku::unsafeProxy(vk::Format::eB8G8R8A8Srgb),
                },
            }.get() } { }
    };
}