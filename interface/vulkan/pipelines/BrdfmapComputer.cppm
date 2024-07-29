export module vk_gltf_viewer:vulkan.pipelines.BrdfmapComputer;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::pipelines {
    export struct BrdfmapComputer {
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1> {
            explicit DescriptorSetLayouts(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::ImageView brdfmapImageView
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &brdfmapInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setImageInfo(brdfmapInfo),
                        };
                    },
                    vk::DescriptorImageInfo { {}, brdfmapImageView, vk::ImageLayout::eGeneral },
                };
            }
        };

        struct SpecializationConstants {
            std::uint32_t numSamples;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        BrdfmapComputer(const vk::raii::Device &device [[clang::lifetimebound]], const SpecializationConstants &specializationConstants = { 1024 });

        auto compute(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const vk::Extent2D &imageSize) const -> void;
    };
}