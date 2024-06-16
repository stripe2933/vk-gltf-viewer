module;

#include <cstdint>
#include <array>
#include <compare>
#include <string_view>

#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.pipelines.Rec709Renderer;

export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan::pipelines {
    export class Rec709Renderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::ImageView inputImageView
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &inputImageInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setImageInfo(inputImageInfo),
                        };
                    },
                    vk::DescriptorImageInfo { {}, inputImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                };
            }
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        Rec709Renderer(const vk::raii::Device &device, vk::RenderPass renderPass, std::uint32_t subpass, const shaderc::Compiler &compiler);

        auto draw(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets) const -> void;

    private:
        static std::string_view vert, frag;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, vk::RenderPass renderPass, std::uint32_t subpass, const shaderc::Compiler &compiler) const -> decltype(pipeline);
    };
}