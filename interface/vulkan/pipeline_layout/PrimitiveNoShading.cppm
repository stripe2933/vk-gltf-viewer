module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pl.PrimitiveNoShading;

import std;
export import glm;
export import vulkan_hpp;
import vku;
export import :vulkan.dsl.Asset;

namespace vk_gltf_viewer::vulkan::pl {
    export struct PrimitiveNoShading : vk::raii::PipelineLayout {
        struct PushConstant {
            glm::mat4 projectionView;
        };

        PrimitiveNoShading(
            const vk::raii::Device &device LIFETIMEBOUND,
            const dsl::Asset& descriptorSetLayout
        ) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eVertex,
                    0, sizeof(PushConstant),
                }),
            } } { }

        void pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const {
            commandBuffer.pushConstants<PushConstant>(**this, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
        }
    };
}