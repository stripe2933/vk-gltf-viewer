export module vk_gltf_viewer:vulkan.pipeline.DepthRenderer;

import std;
export import glm;
export import vku;
export import :vulkan.dsl.Scene;

namespace vk_gltf_viewer::vulkan::pipeline {
    export struct DepthRenderer {
        struct PushConstant {
            glm::mat4 projectionView;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        DepthRenderer(const vk::raii::Device &device [[clang::lifetimebound]], const dsl::Scene& descriptorSetLayout [[clang::lifetimebound]]);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;
    };
}