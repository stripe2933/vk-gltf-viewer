module;

#include <compare>
#include <string_view>

#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.pipelines.MeshRenderer;

export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan {
    export class MeshRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1, 1>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device);
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

            [[nodiscard]] auto getDescriptorWrites1(const vk::DescriptorBufferInfo &nodeTransformBufferInfo) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorBufferInfo &nodeTransformBufferInfo) {
                        return std::array {
                            getDescriptorWrite<1, 0>().setBufferInfo(nodeTransformBufferInfo),
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
        };

        // Pipeline resource types.
        struct Camera { glm::mat4 projectionView; glm::vec3 viewPosition; };
        struct NodeTransform { glm::mat4 matrix; glm::mat4 inverseMatrix = inverse(matrix); };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit MeshRenderer(const vk::raii::Device &device, const shaderc::Compiler &compiler);

        auto draw(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, vk::Buffer indexBuffer, vk::DeviceSize indexBufferOffset, vk::IndexType indexType, std::uint32_t drawCount, const PushConstant &pushConstant) const -> void;

    private:
        static std::string_view vert, frag;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> decltype(pipeline);
    };
};