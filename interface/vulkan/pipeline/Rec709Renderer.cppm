export module vk_gltf_viewer:vulkan.pipeline.Rec709Renderer;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::pipeline {
    export class Rec709Renderer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage>{
            explicit DescriptorSetLayout(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit Rec709Renderer(const vk::raii::Device &device [[clang::lifetimebound]]);

        auto draw(vk::CommandBuffer commandBuffer, vku::DescriptorSet<DescriptorSetLayout> descriptorSets, const vk::Offset2D &passthruOffset) const -> void;

    private:
        struct PushConstant;
    };
}