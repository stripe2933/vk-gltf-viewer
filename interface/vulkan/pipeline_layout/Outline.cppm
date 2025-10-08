module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.Outline;

#ifdef _MSC_VER
import std;
#endif
export import glm;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.Outline;

namespace vk_gltf_viewer::vulkan::pl {
    export struct Outline final : vk::raii::PipelineLayout {
        struct PushConstant {
            static constexpr vk::PushConstantRange range = {
                vk::ShaderStageFlagBits::eFragment,
                0, 20,
            };

            glm::vec4 outlineColor;
            float outlineThickness;
        };

        Outline(const vk::raii::Device &device LIFETIMEBOUND, const dsl::Outline &descriptorSetLayout LIFETIMEBOUND);
    };
}

module :private;

vk_gltf_viewer::vulkan::pl::Outline::Outline(
    const vk::raii::Device &device,
    const dsl::Outline &descriptorSetLayout
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        PushConstant::range,
    } } { }