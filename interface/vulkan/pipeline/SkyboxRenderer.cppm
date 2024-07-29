export module vk_gltf_viewer:vulkan.pipeline.SkyboxRenderer;

import std;
export import glm;
export import :vulkan.buffer.CubeIndices;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::pipeline {
    export class SkyboxRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1>{
            DescriptorSetLayouts(const vk::raii::Device &device [[clang::lifetimebound]], const vk::Sampler &sampler);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::ImageView skyboxImageView
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &skyboxInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setImageInfo(skyboxInfo),
                        };
                    },
                    vk::DescriptorImageInfo { {}, skyboxImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                };
            }
        };

        struct PushConstant {
            glm::mat4 projectionView;
        };

        vk::raii::Sampler sampler;
        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SkyboxRenderer(const Gpu &gpu [[clang::lifetimebound]], const buffer::CubeIndices &cubeIndices [[clang::lifetimebound]]);

        auto draw(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const PushConstant &pushConstant) const -> void;

    private:
        const buffer::CubeIndices &cubeIndices;
    };
}