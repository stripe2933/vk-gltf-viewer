module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pl.PrimitiveNoShading;

import std;
export import glm;
export import vulkan_hpp;
import vku;
export import :vulkan.dsl.Asset;
export import :vulkan.dsl.Scene;

namespace vk_gltf_viewer::vulkan::pl {
    export struct PrimitiveNoShading : vk::raii::PipelineLayout {
        struct PushConstant {
            glm::mat4 projectionView;
        };

        PrimitiveNoShading(
            const vk::raii::Device &device [[clang::lifetimebound]],
            std::pair<const dsl::Asset&, const dsl::Scene&> descriptorSetLayouts
        ) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                vku::unsafeProxy({ *descriptorSetLayouts.first, *descriptorSetLayouts.second }),
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eVertex,
                    0, sizeof(PushConstant),
                }),
            } } { }

        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void {
            commandBuffer.pushConstants<PushConstant>(**this, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
        }
    };
}