module;

#include <array>
#include <compare>
#include <span>
#include <string_view>

#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.pipelines.MeshRenderer;

export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan::pipelines {
    export class MeshRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<4, 2, 2>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device, const vk::Sampler &sampler, std::uint32_t textureCount);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                const vk::DescriptorBufferInfo &cameraBufferInfo,
                const vk::DescriptorBufferInfo &cubemapSphericalHarmonicsBufferInfo,
                vk::ImageView prefilteredmapImageView,
                vk::ImageView brdfmapImageView
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorBufferInfo &cameraBufferInfo, const vk::DescriptorBufferInfo &cubemapSphericalHarmonicsBufferInfo, const vk::DescriptorImageInfo &prefilteredmapImageInfo, const vk::DescriptorImageInfo &brdfmapImageInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setBufferInfo(cameraBufferInfo),
                            getDescriptorWrite<0, 1>().setBufferInfo(cubemapSphericalHarmonicsBufferInfo),
                            getDescriptorWrite<0, 2>().setImageInfo(prefilteredmapImageInfo),
                            getDescriptorWrite<0, 3>().setImageInfo(brdfmapImageInfo),
                        };
                    },
                    cameraBufferInfo,
                    cubemapSphericalHarmonicsBufferInfo,
                    vk::DescriptorImageInfo { {}, prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                    vk::DescriptorImageInfo { {}, brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                };
            }

            [[nodiscard]] auto getDescriptorWrites1(
                std::vector<vk::DescriptorImageInfo> textureInfos,
                const vk::DescriptorBufferInfo &materialBufferInfo
            ) const {
                return vku::RefHolder {
                    [this](std::span<const vk::DescriptorImageInfo> textureInfos, const vk::DescriptorBufferInfo &materialBufferInfo) {
                        return std::array {
                            // TODO: Use following line causes C++ module error in MSVC, looks like related to
                            // https://developercommunity.visualstudio.com/t/error-C2028:-structunion-member-must-be/10488679?sort=newest&topics=Fixed-in%3A+Visual+Studio+2017+version+15.2.
                            // Use setPImageInfo method when available in MSVC.
                            // getDescriptorWrite<1, 0>().setPImageInfo(textureInfos),
                            getDescriptorWrite<1, 0>().setDescriptorCount(textureInfos.size()).setPImageInfo(textureInfos.data()),
                            getDescriptorWrite<1, 1>().setBufferInfo(materialBufferInfo),
                        };
                    },
                    std::move(textureInfos),
                    materialBufferInfo,
                };
            }

            [[nodiscard]] auto getDescriptorWrites2(
                const vk::DescriptorBufferInfo &nodeTransformBufferInfo,
                const vk::DescriptorBufferInfo &primitiveBufferInfo
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorBufferInfo &nodeTransformBufferInfo, const vk::DescriptorBufferInfo &primitiveBufferInfo) {
                        return std::array {
                            getDescriptorWrite<2, 0>().setBufferInfo(nodeTransformBufferInfo),
                            getDescriptorWrite<2, 1>().setBufferInfo(primitiveBufferInfo),
                        };
                    },
                    nodeTransformBufferInfo,
                    primitiveBufferInfo,
                };
            }
        };

        struct PushConstant {
            std::uint32_t primitiveIndex;
        };

        // Pipeline resource types.
        struct Camera {
            glm::mat4 projectionView;
            glm::vec3 viewPosition;
        };

        vk::raii::Sampler sampler;
        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit MeshRenderer(const vk::raii::Device &device, std::uint32_t textureCount, const shaderc::Compiler &compiler);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto bindDescriptorSets(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t firstSet = 0) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;

    private:
        static std::string_view vert, frag;

        [[nodiscard]] auto createSampler(const vk::raii::Device &device) const -> decltype(sampler);
        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> decltype(pipeline);
    };
}