export module vk_gltf_viewer:vulkan.pipeline.SphericalHarmonicsRenderer;

import std;
export import glm;
export import :vulkan.buffer.CubeIndices;
export import :vulkan.dsl.ImageBasedLighting;

namespace vk_gltf_viewer::vulkan::pipeline {
    export class SphericalHarmonicsRenderer {
    public:
        struct PushConstant {
            glm::mat4 projectionView;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SphericalHarmonicsRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const dsl::ImageBasedLighting &descriptorSetLayout [[clang::lifetimebound]],
            const buffer::CubeIndices &cubeIndices [[clang::lifetimebound]]);

        auto draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<dsl::ImageBasedLighting> descriptorSet, const PushConstant &pushConstant) const -> void;

    private:
        const buffer::CubeIndices &cubeIndices;
    };
}