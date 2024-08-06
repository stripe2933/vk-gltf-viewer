export module vk_gltf_viewer:vulkan.pipeline.PrimitiveRenderer;

import std;
export import glm;
export import vku;
export import :vulkan.dsl.ImageBasedLighting;
export import :vulkan.dsl.Asset;
export import :vulkan.dsl.Scene;

namespace vk_gltf_viewer::vulkan::pipeline {
    export struct PrimitiveRenderer {
        struct PushConstant {
            glm::mat4 projectionView;
            glm::vec3 viewPosition;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        PrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            std::tuple<const dsl::ImageBasedLighting&, const dsl::Asset&, const dsl::Scene&> descriptorSetLayouts [[clang::lifetimebound]]);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;
    };
}