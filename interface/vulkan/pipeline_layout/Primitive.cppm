module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pl.Primitive;

import std;
export import glm;
export import vulkan_hpp;
import vku;
export import :vulkan.dsl.Asset;
export import :vulkan.dsl.ImageBasedLighting;

namespace vk_gltf_viewer::vulkan::pl {
    export struct Primitive : vk::raii::PipelineLayout {
        struct PushConstant {
            glm::mat4 projectionView;
            glm::vec3 viewPosition;
        };

        Primitive(
            const vk::raii::Device &device LIFETIMEBOUND,
            std::pair<const dsl::ImageBasedLighting&, const dsl::Asset&> descriptorSetLayouts LIFETIMEBOUND
        ) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                vku::unsafeProxy({ *descriptorSetLayouts.first, *descriptorSetLayouts.second }),
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eAllGraphics,
                    0, sizeof(PushConstant),
                }),
            } } { }

        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void {
            commandBuffer.pushConstants<PushConstant>(**this, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
        }
    };
}