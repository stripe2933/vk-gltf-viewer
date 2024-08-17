export module vk_gltf_viewer:vulkan.pipeline.BrdfmapComputer;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct BrdfmapComputer {
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
            explicit DescriptorSetLayout(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        struct SpecializationConstants {
            std::uint32_t numSamples;
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit BrdfmapComputer(const vk::raii::Device &device [[clang::lifetimebound]], const SpecializationConstants &specializationConstants = { 1024 });

        auto compute(vk::CommandBuffer commandBuffer, vku::DescriptorSet<DescriptorSetLayout> descriptorSet, const vk::Extent2D &imageSize) const -> void;
    };
}