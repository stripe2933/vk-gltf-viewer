export module vk_gltf_viewer:vulkan.pipeline.DepthRenderer;

import std;
export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan::pipeline {
    export struct DepthRenderer {
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<2>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                const vk::DescriptorBufferInfo &primitiveBufferInfo [[clang::lifetimebound]],
                const vk::DescriptorBufferInfo &nodeTransformBufferInfo [[clang::lifetimebound]]
            ) const -> std::array<vk::WriteDescriptorSet, 2>;
        };

        struct PushConstant {
            glm::mat4 projectionView;
            std::uint32_t hoveringNodeIndex;
            std::uint32_t selectedNodeIndex;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit DepthRenderer(const vk::raii::Device &device [[clang::lifetimebound]]);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto bindDescriptorSets(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t firstSet = 0) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;
    };
}