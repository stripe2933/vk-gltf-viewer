export module vk_gltf_viewer:vulkan.pipeline.OutlineRenderer;

import std;
export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct OutlineRenderer {
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage>{
            explicit DescriptorSetLayout(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        struct PushConstant {
            glm::vec4 outlineColor;
            glm::i32vec2 passthruOffset;
            float outlineThickness;
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit OutlineRenderer(const vk::raii::Device &device [[clang::lifetimebound]]);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto bindDescriptorSets(vk::CommandBuffer commandBuffer, vku::DescriptorSet<DescriptorSetLayout> descriptorSet) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;
        auto draw(vk::CommandBuffer commandBuffer) const -> void;
    };
}