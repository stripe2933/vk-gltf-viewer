export module vk_gltf_viewer:vulkan.pipelines.Rec709Renderer;

import std;
export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan::pipelines {
    export class Rec709Renderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::ImageView hdriImageView
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &hdriImageInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setImageInfo(hdriImageInfo),
                        };
                    },
                    vk::DescriptorImageInfo { {}, hdriImageView, vk::ImageLayout::eGeneral },
                };
            }
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit Rec709Renderer(const vk::raii::Device &device [[clang::lifetimebound]]);

        auto draw(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const vk::Offset2D &passthruOffset) const -> void;

    private:
        struct PushConstant {
            glm::i32vec2 hdriImageOffset;
        };

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device) const -> decltype(pipeline);
    };
}