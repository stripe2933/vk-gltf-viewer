module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.pl.PrimitiveBoundingVolume;

import std;
export import glm;
import vku;
export import :vulkan.dsl.Asset;

namespace vk_gltf_viewer::vulkan::pl {
    export struct PrimitiveBoundingVolume : vk::raii::PipelineLayout {
        struct PushConstant {
            static constexpr vk::PushConstantRange range {
                vk::ShaderStageFlagBits::eAllGraphics,
                0, 84,
            };

            glm::mat4 projectionView;
            glm::vec4 color;
            float enlarge;
        };

        PrimitiveBoundingVolume(
            const vk::raii::Device &device LIFETIMEBOUND,
            const dsl::Asset& assetDescriptorSetLayout LIFETIMEBOUND
        ) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                *assetDescriptorSetLayout,
                PushConstant::range,
            } } { }

        void pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const {
            commandBuffer.pushConstants<PushConstant>(**this, PushConstant::range.stageFlags, 0, pushConstant);
        }
    };
}