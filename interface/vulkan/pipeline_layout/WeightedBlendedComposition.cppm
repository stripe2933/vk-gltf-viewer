module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.WeightedBlendedComposition;

import std;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.WeightedBlendedComposition;

namespace vk_gltf_viewer::vulkan::pl {
    export struct WeightedBlendedComposition : vk::raii::PipelineLayout {
        WeightedBlendedComposition(const vk::raii::Device &device LIFETIMEBOUND, const dsl::WeightedBlendedComposition &descriptorSetLayout LIFETIMEBOUND);
    };
}

module :private;

vk_gltf_viewer::vulkan::pl::WeightedBlendedComposition::WeightedBlendedComposition(
    const vk::raii::Device &device,
    const dsl::WeightedBlendedComposition &descriptorSetLayout
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
    } } { }