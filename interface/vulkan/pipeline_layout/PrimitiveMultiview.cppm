module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pl.PrimitiveMultiview;

import std;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.dsl.Asset;
export import vk_gltf_viewer.vulkan.dsl.Renderer;

namespace vk_gltf_viewer::vulkan::pl {
    export struct PrimitiveMultiview : vk::raii::PipelineLayout {
        PrimitiveMultiview(
            const vk::raii::Device &device LIFETIMEBOUND,
            std::pair<const dsl::Renderer&, const dsl::Asset&> descriptorSetLayouts LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pl::PrimitiveMultiview::PrimitiveMultiview(
    const vk::raii::Device &device,
    std::pair<const dsl::Renderer&, const dsl::Asset&> descriptorSetLayouts
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy({ *descriptorSetLayouts.first, *descriptorSetLayouts.second }),
    } } { }