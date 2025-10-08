module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.InverseToneMapping;

#ifdef _MSC_VER
import std;
#endif
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.InverseToneMapping;

namespace vk_gltf_viewer::vulkan::pl {
    export struct InverseToneMapping final : vk::raii::PipelineLayout {
        InverseToneMapping(const vk::raii::Device &device LIFETIMEBOUND, const dsl::InverseToneMapping &descriptorSetLayout LIFETIMEBOUND);
    };
}

module :private;

vk_gltf_viewer::vulkan::pl::InverseToneMapping::InverseToneMapping(
    const vk::raii::Device &device,
    const dsl::InverseToneMapping &descriptorSetLayout
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
    } } { }