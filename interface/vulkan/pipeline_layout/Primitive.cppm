module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.Primitive;

import std;
export import glm;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.Asset;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.ImageBasedLighting;

namespace vk_gltf_viewer::vulkan::pl {
    export struct Primitive : vk::raii::PipelineLayout {
        struct PushConstant {
            glm::mat4 projectionView;
            glm::vec3 viewPosition;
        };

        Primitive(
            const vk::raii::Device &device LIFETIMEBOUND,
            std::pair<const dsl::ImageBasedLighting&, const dsl::Asset&> descriptorSetLayouts LIFETIMEBOUND
        );

        void pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pl::Primitive::Primitive(
    const vk::raii::Device &device,
    std::pair<const dsl::ImageBasedLighting&, const dsl::Asset&> descriptorSetLayouts
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy({ *descriptorSetLayouts.first, *descriptorSetLayouts.second }),
        vku::unsafeProxy(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eAllGraphics,
            0, sizeof(PushConstant),
        }),
    } } { }

void vk_gltf_viewer::vulkan::pl::Primitive::pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const {
    commandBuffer.pushConstants<PushConstant>(**this, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
}