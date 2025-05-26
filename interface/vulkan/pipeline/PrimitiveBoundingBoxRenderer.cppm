module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pipeline.PrimitiveBoundingBoxRenderer;

#ifdef _MSC_VER
import std;
#endif
export import glm;
import vku;
import :shader.primitive_bounding_box_vert;
import :shader.primitive_bounding_volume_frag;
export import :vulkan.pl.PrimitiveBoundingVolume;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct PrimitiveBoundingBoxRenderer : vk::raii::Pipeline {
        PrimitiveBoundingBoxRenderer(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::PrimitiveBoundingVolume &pipelineLayout LIFETIMEBOUND,
            const rp::Scene &sceneRenderPass LIFETIMEBOUND
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::primitive_bounding_box_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::primitive_bounding_volume_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                    {},
                    false, false,
                    vk::PolygonMode::eFill,
                    // Translucent objects' back faces shouldn't be culled.
                    vk::CullModeFlagBits::eNone, {},
                    {}, {}, {}, {},
                    1.f,
                }))
                .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                    {},
                    // Translucent objects shouldn't interfere with the pre-rendered depth buffer. Use reverse Z.
                    true, false, vk::CompareOp::eGreater,
                }))
                .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                    {},
                    false, {},
                    vku::unsafeProxy({
                        vk::PipelineColorBlendAttachmentState {
                            true,
                            vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                            vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                        },
                        vk::PipelineColorBlendAttachmentState {
                            true,
                            vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcColor, vk::BlendOp::eAdd,
                            vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                            vk::ColorComponentFlagBits::eR,
                        },
                    }),
                    { 1.f, 1.f, 1.f, 1.f },
                }))
                .setRenderPass(*sceneRenderPass)
                .setSubpass(1),
            } { }
    };
}