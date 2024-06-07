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
        struct PushConstant {
            glm::mat4 model;
            glm::mat4 projectionView;
            vk::DeviceAddress pPositionBuffer;
            vk::DeviceAddress pNormalBuffer;
            std::uint8_t positionByteStride;
            std::uint8_t normalByteStride;
            char padding[14];
            glm::vec3 viewPosition;
        };

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit MeshRenderer(const vk::raii::Device &device, const shaderc::Compiler &compiler);

        auto draw(vk::CommandBuffer commandBuffer, vk::Buffer indexBuffer, vk::DeviceSize indexBufferOffset, vk::IndexType indexType, std::uint32_t drawCount, const PushConstant &pushConstant) const -> void;

    private:
        static std::string_view vert, frag;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> decltype(pipeline);
    };
};