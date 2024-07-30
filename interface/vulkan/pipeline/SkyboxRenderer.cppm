export module vk_gltf_viewer:vulkan.pipeline.SkyboxRenderer;

import std;
export import glm;
export import :vulkan.buffer.CubeIndices;
export import :vulkan.sampler.CubemapSampler;

namespace vk_gltf_viewer::vulkan::pipeline {
    export class SkyboxRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1>{
            DescriptorSetLayouts(const vk::raii::Device &device [[clang::lifetimebound]], const CubemapSampler &sampler [[clang::lifetimebound]]);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::ImageView skyboxImageView
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &skyboxInfo) {
                        return getDescriptorWrite<0, 0>().setImageInfo(skyboxInfo);
                    },
                    vk::DescriptorImageInfo { {}, skyboxImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                };
            }
        };

        struct PushConstant {
            glm::mat4 projectionView;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SkyboxRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const CubemapSampler &sampler [[clang::lifetimebound]],
            const buffer::CubeIndices &cubeIndices [[clang::lifetimebound]]);

        auto draw(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const PushConstant &pushConstant) const -> void;

    private:
        const buffer::CubeIndices &cubeIndices;
    };
}