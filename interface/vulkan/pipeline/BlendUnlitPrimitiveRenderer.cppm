export module vk_gltf_viewer:vulkan.pipeline.BlendUnlitPrimitiveRenderer;

import vku;
import :shader.unlit_primitive_vert;
import :shader.blend_unlit_primitive_frag;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct BlendUnlitPrimitiveRenderer : vk::raii::Pipeline {
        BlendUnlitPrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::Primitive &layout [[clang::lifetimebound]],
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::unlit_primitive_vert, vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::blend_unlit_primitive_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                *layout, 1, true, vk::SampleCountFlagBits::e4)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                // Translucent objects' back faces shouldn't be culled.
                vk::CullModeFlagBits::eNone, {},
                false, {}, {}, {},
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
            .setSubpass(1)
        } { }
    };
}