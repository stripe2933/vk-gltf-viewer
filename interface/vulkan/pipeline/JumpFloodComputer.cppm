export module vk_gltf_viewer:vulkan.pipeline.JumpFloodComputer;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class JumpFloodComputer {
    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
            explicit DescriptorSetLayout(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit JumpFloodComputer(const vk::raii::Device &device [[clang::lifetimebound]]);

        [[nodiscard]] auto compute(
            vk::CommandBuffer commandBuffer,
            vku::DescriptorSet<DescriptorSetLayout> descriptorSets,
            std::uint32_t initialSampleOffset,
            const vk::Extent2D &imageExtent) const -> vk::Bool32;

    private:
        struct PushConstant;
    };
}