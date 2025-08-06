module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.PrimitiveNoShading;

import std;
export import glm;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.Asset;

namespace vk_gltf_viewer::vulkan::pl {
    export struct PrimitiveNoShading : vk::raii::PipelineLayout {
        struct PushConstant {
            glm::mat4 projectionView;
        };

        PrimitiveNoShading(const vk::raii::Device &device LIFETIMEBOUND, const dsl::Asset& descriptorSetLayout);

        void pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pl::PrimitiveNoShading::PrimitiveNoShading(
    const vk::raii::Device &device,
    const dsl::Asset& descriptorSetLayout
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        vku::unsafeProxy(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eVertex,
            0, sizeof(PushConstant),
        }),
    } } { }

void vk_gltf_viewer::vulkan::pl::PrimitiveNoShading::pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const {
    commandBuffer.pushConstants<PushConstant>(**this, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
}