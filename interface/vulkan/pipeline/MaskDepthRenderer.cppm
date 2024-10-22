export module vk_gltf_viewer:vulkan.pipeline.MaskDepthRenderer;

import std;
export import glm;
export import :vulkan.dsl.Asset;
export import :vulkan.dsl.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct MaskDepthRenderer {
        struct PushConstant {
            glm::mat4 projectionView;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        MaskDepthRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            std::tuple<const dsl::Scene&, const dsl::Asset&> descriptorSetLayouts [[clang::lifetimebound]]);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;
    };
}