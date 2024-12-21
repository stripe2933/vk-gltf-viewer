export module vk_gltf_viewer:vulkan.pipeline.MaskPrimitiveRenderer;

import std;
import vku;
import :shader.faceted_primitive_vert;
import :shader.primitive_vert;
import :shader.mask_faceted_primitive_frag;
import :shader.mask_primitive_frag;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct MaskPrimitiveRenderer : vk::raii::Pipeline {
        MaskPrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::Primitive &layout [[clang::lifetimebound]],
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]],
            bool fragmentShaderTBN
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader {
                        fragmentShaderTBN
                            ? std::span<const std::uint32_t> { shader::faceted_primitive_vert }
                            : std::span<const std::uint32_t> { shader::primitive_vert },
                        vk::ShaderStageFlagBits::eVertex,
                    },
                    vku::Shader {
                        fragmentShaderTBN
                            ? std::span<const std::uint32_t> { shader::mask_faceted_primitive_frag }
                            : std::span<const std::uint32_t> { shader::mask_primitive_frag },
                        vk::ShaderStageFlagBits::eFragment,
                    }).get(),
                *layout, 1, true, vk::SampleCountFlagBits::e4)
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, true, vk::CompareOp::eGreater, // Use reverse Z.
            }))
            .setPMultisampleState(vku::unsafeAddress(vk::PipelineMultisampleStateCreateInfo {
                {},
                vk::SampleCountFlagBits::e4,
                {}, {}, {},
                true,
            }))
            .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                    vk::DynamicState::eCullMode,
                }),
            }))
            .setRenderPass(*sceneRenderPass)
            .setSubpass(0)
        } { }
    };
}