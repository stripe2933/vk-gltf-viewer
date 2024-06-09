module;

#include <cstdint>
#include <compare>
#include <span>
#include <string_view>

#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.pipelines.MeshRenderer;

export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan {
    export class MeshRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1, 2, 1>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device, std::uint32_t textureCount);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(const vk::DescriptorBufferInfo &cameraBufferInfo) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorBufferInfo &cameraBufferInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setBufferInfo(cameraBufferInfo),
                        };
                    },
                    cameraBufferInfo,
                };
            }

            [[nodiscard]] auto getDescriptorWrites1(
                std::vector<vk::DescriptorImageInfo> textureInfos,
                const vk::DescriptorBufferInfo &materialBufferInfo
            ) const {
                return vku::RefHolder {
                    [this](std::span<const vk::DescriptorImageInfo> textureInfos, const vk::DescriptorBufferInfo &materialBufferInfo) {
                        return std::array {
                            getDescriptorWrite<1, 0>().setImageInfo(textureInfos),
                            getDescriptorWrite<1, 1>().setBufferInfo(materialBufferInfo),
                        };
                    },
                    std::move(textureInfos),
                    materialBufferInfo,
                };
            }

            [[nodiscard]] auto getDescriptorWrites2(const vk::DescriptorBufferInfo &nodeTransformBufferInfo) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorBufferInfo &nodeTransformBufferInfo) {
                        return std::array {
                            getDescriptorWrite<2, 0>().setBufferInfo(nodeTransformBufferInfo),
                        };
                    },
                    nodeTransformBufferInfo,
                };
            }
        };

        struct PushConstant {
            vk::DeviceAddress pPositionBuffer;
            vk::DeviceAddress pNormalBuffer;
            std::uint8_t positionByteStride;
            std::uint8_t normalByteStride;
            char padding[14];
            std::uint32_t nodeIndex;
            std::uint32_t materialIndex;
        };

        // Pipeline resource types.
        struct Camera {
            glm::mat4 projectionView;
            glm::vec3 viewPosition;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit MeshRenderer(const vk::raii::Device &device, std::uint32_t textureCount, const shaderc::Compiler &compiler);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto bindDescriptorSets(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t firstSet = 0) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;

    private:
        static std::string_view vert, frag;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> decltype(pipeline);
    };
};