module;

#include <array>
#include <compare>
#include <string_view>

#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.pipelines.SphericalHarmonicsRenderer;

export import glm;
export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::pipelines {
    export class SphericalHarmonicsRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                const vk::DescriptorBufferInfo &cubemapSphericalHarmonicsBufferInfo [[clang::lifetimebound]]
            ) const -> std::array<vk::WriteDescriptorSet, 1>;
        };

        struct PushConstant {
            glm::mat4 projectionView;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;
        vku::MappedBuffer indexBuffer;

        SphericalHarmonicsRenderer(const Gpu &gpu, const shaderc::Compiler &compiler);

        auto draw(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const PushConstant &pushConstant) const -> void;

    private:
        static std::string_view vert, frag;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> decltype(pipeline);
        [[nodiscard]] auto createIndexBuffer(vma::Allocator allocator) const -> decltype(indexBuffer);
    };
}