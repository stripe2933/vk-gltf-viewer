module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.Primitive;

import std;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.Asset;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.ImageBasedLighting;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.Renderer;

namespace vk_gltf_viewer::vulkan::pl {
    export struct Primitive : vk::raii::PipelineLayout {
        Primitive(
            const vk::raii::Device &device LIFETIMEBOUND,
            std::tuple<const dsl::Renderer&, const dsl::ImageBasedLighting&, const dsl::Asset&> descriptorSetLayouts LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::pl::Primitive::Primitive(
    const vk::raii::Device &device,
    std::tuple<const dsl::Renderer&, const dsl::ImageBasedLighting&, const dsl::Asset&> descriptorSetLayouts
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy({ *get<0>(descriptorSetLayouts), *get<1>(descriptorSetLayouts), *get<2>(descriptorSetLayouts) }),
    } } { }