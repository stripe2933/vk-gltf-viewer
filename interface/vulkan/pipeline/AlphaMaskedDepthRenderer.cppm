export module vk_gltf_viewer:vulkan.pipeline.AlphaMaskedDepthRenderer;

import std;
export import glm;
export import vku;
export import :vulkan.dsl.Asset;
export import :vulkan.dsl.Scene;

namespace vk_gltf_viewer::vulkan::pipeline {
    export struct AlphaMaskedDepthRenderer {
        struct PushConstant {
            glm::mat4 projectionView;
            std::uint32_t hoveringNodeIndex;
            std::uint32_t selectedNodeIndex;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        AlphaMaskedDepthRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            std::tuple<const dsl::Scene&, const dsl::Asset&> descriptorSetLayouts [[clang::lifetimebound]]);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;
    };
}