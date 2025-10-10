module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.PrimitiveNoShading;

import std;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.Asset;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.Renderer;

namespace vk_gltf_viewer::vulkan::pl {
    export struct PrimitiveNoShading final : vk::raii::PipelineLayout {
        PrimitiveNoShading(
            const vk::raii::Device &device LIFETIMEBOUND,
            std::pair<const dsl::Renderer&, const dsl::Asset&> descriptorSetLayouts
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pl::PrimitiveNoShading::PrimitiveNoShading(
    const vk::raii::Device &device,
    std::pair<const dsl::Renderer&, const dsl::Asset&> descriptorSetLayouts
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::lvalue({ *descriptorSetLayouts.first, *descriptorSetLayouts.second }),
    } } { }