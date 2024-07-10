export module vk_gltf_viewer:vulkan.pipelines.SkyboxRenderer;

import std;
export import glm;
export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::pipelines {
    export class SkyboxRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1>{
            DescriptorSetLayouts(const vk::raii::Device &device, const vk::Sampler &sampler);
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
        vku::MappedBuffer indexBuffer;

        explicit SkyboxRenderer(const Gpu &gpu);

        auto draw(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const PushConstant &pushConstant) const -> void;

    private:
        [[nodiscard]] auto createSampler(const vk::raii::Device &device) const -> decltype(sampler);
        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device) const -> decltype(pipeline);
        [[nodiscard]] auto createIndexBuffer(vma::Allocator allocator) const -> decltype(indexBuffer);
    };
}