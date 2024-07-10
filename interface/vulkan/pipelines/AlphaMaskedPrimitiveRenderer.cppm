export module vk_gltf_viewer:vulkan.pipelines.AlphaMaskedPrimitiveRenderer;

import std;
export import glm;
export import vku;
export import :vulkan.pipelines.PrimitiveRenderer;

namespace vk_gltf_viewer::vulkan::pipelines {
    export class AlphaMaskedPrimitiveRenderer {
    public:
        struct PushConstant {
            glm::mat4 projectionView;
            glm::vec3 viewPosition;
        };

        vk::raii::Pipeline pipeline;

        AlphaMaskedPrimitiveRenderer(const vk::raii::Device &device [[clang::lifetimebound]], vk::PipelineLayout primitiveRendererPipelineLayout);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;

    private:
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, vk::PipelineLayout primitiveRendererPipelineLayout) const -> decltype(pipeline);
    };
}