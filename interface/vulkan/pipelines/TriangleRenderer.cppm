module;

#include <compare>
#include <string_view>

#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.pipelines.TriangleRenderer;

export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan {
    export class TriangleRenderer {
    public:
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit TriangleRenderer(const vk::raii::Device &device, const shaderc::Compiler &compiler);

        auto draw(vk::CommandBuffer commandBuffer) const -> void;

    private:
        static std::string_view vert, frag;

        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> decltype(pipeline);
    };
};