module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.MousePicking;

import std;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.Asset;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.MousePicking;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.Renderer;

namespace vk_gltf_viewer::vulkan::pl {
    export struct MousePicking : vk::raii::PipelineLayout {
        struct PushConstant {
            static constexpr vk::PushConstantRange range {
                vk::ShaderStageFlagBits::eVertex,
                0, 4,
            };

            std::uint32_t viewIndex;
        };

        MousePicking(
            const vk::raii::Device &device LIFETIMEBOUND,
            std::tuple<const dsl::Renderer&, const dsl::Asset&, const dsl::MousePicking&> descriptorSetLayouts
        );
    };
}

module :private;

vk_gltf_viewer::vulkan::pl::MousePicking::MousePicking(
    const vk::raii::Device &device,
    std::tuple<const dsl::Renderer&, const dsl::Asset&, const dsl::MousePicking&> descriptorSetLayouts
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy({ *get<0>(descriptorSetLayouts), *get<1>(descriptorSetLayouts), *get<2>(descriptorSetLayouts) }),
        PushConstant::range,
    } } { }