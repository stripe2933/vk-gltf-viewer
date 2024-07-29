export module vk_gltf_viewer:vulkan.pipeline.PrimitiveRenderer;

import std;
export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan::pipeline {
    export struct PrimitiveRenderer {
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<3, 2, 2>{
            DescriptorSetLayouts(const vk::raii::Device &device [[clang::lifetimebound]], const vk::Sampler &sampler, std::uint32_t textureCount);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                const vk::DescriptorBufferInfo &cubemapSphericalHarmonicsBufferInfo [[clang::lifetimebound]],
                vk::ImageView prefilteredmapImageView,
                vk::ImageView brdfmapImageView
            ) const {
                return vku::RefHolder {
                    [&](const vk::DescriptorImageInfo &prefilteredmapImageInfo, const vk::DescriptorImageInfo &brdfmapImageInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setBufferInfo(cubemapSphericalHarmonicsBufferInfo),
                            getDescriptorWrite<0, 1>().setImageInfo(prefilteredmapImageInfo),
                            getDescriptorWrite<0, 2>().setImageInfo(brdfmapImageInfo),
                        };
                    },
                    vk::DescriptorImageInfo { {}, prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                    vk::DescriptorImageInfo { {}, brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                };
            }

            [[nodiscard]] auto getDescriptorWrites1(
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
                            getDescriptorWrite<1, 0>().setDescriptorCount(combinedTextureInfos.size()).setPImageInfo(combinedTextureInfos.data()),
                            getDescriptorWrite<1, 1>().setBufferInfo(materialBufferInfo),
                        };
                    },
                    std::move(combinedTextureInfos),
                };
            }

            [[nodiscard]] auto getDescriptorWrites2(
                const vk::DescriptorBufferInfo &primitiveBufferInfo [[clang::lifetimebound]],
                const vk::DescriptorBufferInfo &nodeTransformBufferInfo [[clang::lifetimebound]]
            ) const -> std::array<vk::WriteDescriptorSet, 2>;
        };

        struct PushConstant {
            glm::mat4 projectionView;
            glm::vec3 viewPosition;
        };

        vk::raii::Sampler sampler;
        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        PrimitiveRenderer(const vk::raii::Device &device [[clang::lifetimebound]], std::uint32_t textureCount);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto bindDescriptorSets(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t firstSet = 0) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;
    };
}