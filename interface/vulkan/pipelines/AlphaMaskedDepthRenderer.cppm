module;

#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.pipelines.AlphaMaskedDepthRenderer;

import std;
export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan::pipelines {
    export class AlphaMaskedDepthRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<2, 2>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device, std::uint32_t textureCount);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                const vk::DescriptorImageInfo &fallbackTextureInfo,
                std::span<const vk::DescriptorImageInfo> textureInfos,
                const vk::DescriptorBufferInfo &materialBufferInfo [[clang::lifetimebound]]
            ) const {
                std::vector<vk::DescriptorImageInfo> combinedTextureInfos;
                combinedTextureInfos.reserve(1 /* fallbackTextureInfo */ + textureInfos.size());
                combinedTextureInfos.push_back(fallbackTextureInfo);
                combinedTextureInfos.append_range(textureInfos);

                return vku::RefHolder {
                    [&](std::span<const vk::DescriptorImageInfo> combinedTextureInfos) {
                        return std::array {
                            // TODO: Use following line causes C++ module error in MSVC, looks like related to
                            // https://developercommunity.visualstudio.com/t/error-C2028:-structunion-member-must-be/10488679?sort=newest&topics=Fixed-in%3A+Visual+Studio+2017+version+15.2.
                            // Use setImageInfo method when available in MSVC.
                            // getDescriptorWrite<1, 0>().setImageInfo(combinedTextureInfos),
                            getDescriptorWrite<0, 0>().setDescriptorCount(combinedTextureInfos.size()).setPImageInfo(combinedTextureInfos.data()),
                            getDescriptorWrite<0, 1>().setBufferInfo(materialBufferInfo),
                        };
                    },
                    std::move(combinedTextureInfos),
                };
            }

            [[nodiscard]] auto getDescriptorWrites1(
                const vk::DescriptorBufferInfo &primitiveBufferInfo [[clang::lifetimebound]],
                const vk::DescriptorBufferInfo &nodeTransformBufferInfo [[clang::lifetimebound]]
            ) const -> std::array<vk::WriteDescriptorSet, 2>;
        };

        struct PushConstant {
            glm::mat4 projectionView;
            std::uint32_t hoveringNodeIndex;
            std::uint32_t selectedNodeIndex;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        AlphaMaskedDepthRenderer(const vk::raii::Device &device, std::uint32_t textureCount, const shaderc::Compiler &compiler);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto bindDescriptorSets(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t firstSet = 0) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;

    private:
        static std::string_view vert, frag;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> decltype(pipeline);
    };
}