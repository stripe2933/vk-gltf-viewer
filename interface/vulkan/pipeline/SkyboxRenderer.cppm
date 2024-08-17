export module vk_gltf_viewer:vulkan.pipeline.SkyboxRenderer;

import std;
export import glm;
export import :vulkan.buffer.CubeIndices;
export import :vulkan.dsl.Skybox;

namespace vk_gltf_viewer::vulkan::pipeline {
    export class SkyboxRenderer {
    public:
        struct PushConstant {
            glm::mat4 projectionView;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SkyboxRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const dsl::Skybox &descriptorSetLayout [[clang::lifetimebound]],
            const buffer::CubeIndices &cubeIndices [[clang::lifetimebound]]);

        auto draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<dsl::Skybox> descriptorSet, const PushConstant &pushConstant) const -> void;

    private:
        const buffer::CubeIndices &cubeIndices;
    };
}