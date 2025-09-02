module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.Skybox;

import std;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.Renderer;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.Skybox;

namespace vk_gltf_viewer::vulkan::pl {
    export struct Skybox : vk::raii::PipelineLayout {
        Skybox(
            const vk::raii::Device &device LIFETIMEBOUND,
            std::pair<const dsl::Renderer&, const dsl::Skybox&> descriptorSetLayouts LIFETIMEBOUND
        );
    };
}

module :private;

vk_gltf_viewer::vulkan::pl::Skybox::Skybox(
    const vk::raii::Device &device,
    std::pair<const dsl::Renderer&, const dsl::Skybox&> descriptorSetLayouts
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy({ *descriptorSetLayouts.first, *descriptorSetLayouts.second }),
    } } { }