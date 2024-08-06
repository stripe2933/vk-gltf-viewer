export module vk_gltf_viewer:vulkan.pipeline.SkyboxRenderer;

import std;
export import glm;
export import :vulkan.buffer.CubeIndices;
export import :vulkan.sampler.CubemapSampler;

namespace vk_gltf_viewer::vulkan::pipeline {
    export class SkyboxRenderer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler>{
            DescriptorSetLayout(const vk::raii::Device &device [[clang::lifetimebound]], const CubemapSampler &sampler [[clang::lifetimebound]]);
        };

        struct PushConstant {
            glm::mat4 projectionView;
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SkyboxRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const CubemapSampler &sampler [[clang::lifetimebound]],
            const buffer::CubeIndices &cubeIndices [[clang::lifetimebound]]);

        auto draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<DescriptorSetLayout> descriptorSet, const PushConstant &pushConstant) const -> void;

    private:
        const buffer::CubeIndices &cubeIndices;
    };
}