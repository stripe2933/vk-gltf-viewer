export module vk_gltf_viewer:vulkan.pipeline.MaskFacetedPrimitiveRenderer;

import vku;
export import :vulkan.pipeline.tag;
export import :vulkan.pl.SceneRendering;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct MaskFacetedPrimitiveRenderer : vk::raii::Pipeline {
        MaskFacetedPrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::SceneRendering &layout [[clang::lifetimebound]],
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/faceted_primitive.vert.spv", vk::ShaderStageFlagBits::eVertex),
                vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/mask_faceted_primitive.frag.spv", vk::ShaderStageFlagBits::eFragment)).get(),
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

        /**
         * Construct the pipeline with tessellation shader based TBN matrix generation support.
         * @param device
         * @param layout
         * @param sceneRenderPass
         */
        MaskFacetedPrimitiveRenderer(
            use_tessellation_t,
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::SceneRendering &layout [[clang::lifetimebound]],
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/faceted_primitive.vert.spv", vk::ShaderStageFlagBits::eVertex),
                    vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/faceted_primitive.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl),
                    vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/faceted_primitive.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation),
                    vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/mask_primitive.frag.spv", vk::ShaderStageFlagBits::eFragment)).get(),
                *layout, 1, true, vk::SampleCountFlagBits::e4)
            .setPTessellationState(vku::unsafeAddress(vk::PipelineTessellationStateCreateInfo {
                {},
                3,
            }))
            .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                {},
                vk::PrimitiveTopology::ePatchList,
            }))
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