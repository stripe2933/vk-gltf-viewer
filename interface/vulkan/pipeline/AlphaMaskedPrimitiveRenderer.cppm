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

        vk::raii::Pipeline pipeline;

        AlphaMaskedPrimitiveRenderer(const vk::raii::Device &device [[clang::lifetimebound]], vk::PipelineLayout primitiveRendererPipelineLayout);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
    };
}