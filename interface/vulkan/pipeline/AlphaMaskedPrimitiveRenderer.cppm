export module vk_gltf_viewer:vulkan.pipeline.AlphaMaskedPrimitiveRenderer;

import std;
export import glm;
export import vku;
export import :vulkan.pipeline.PrimitiveRenderer;

namespace vk_gltf_viewer::vulkan::pipeline {
    export struct AlphaMaskedPrimitiveRenderer {
        struct PushConstant {
            glm::mat4 projectionView;
            glm::vec3 viewPosition;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        AlphaMaskedPrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            std::tuple<const dsl::ImageBasedLighting&, const dsl::Asset&, const dsl::Scene&> descriptorSetLayouts [[clang::lifetimebound]]);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
    };
}