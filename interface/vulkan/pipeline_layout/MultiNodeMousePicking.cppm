module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pl.MultiNodeMousePicking;

import std;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.dsl.Asset;
export import vk_gltf_viewer.vulkan.dsl.MultiNodeMousePicking;
export import vk_gltf_viewer.vulkan.dsl.Renderer;

namespace vk_gltf_viewer::vulkan::pl {
    export struct MultiNodeMousePicking : vk::raii::PipelineLayout {
        MultiNodeMousePicking(
            const vk::raii::Device &device LIFETIMEBOUND,
            std::tuple<const dsl::Renderer&, const dsl::Asset&, const dsl::MultiNodeMousePicking&> descriptorSetLayouts LIFETIMEBOUND
        );
    };
}

module :private;

vk_gltf_viewer::vulkan::pl::MultiNodeMousePicking::MultiNodeMousePicking(
    const vk::raii::Device &device,
    std::tuple<const dsl::Renderer&, const dsl::Asset&, const dsl::MultiNodeMousePicking&> descriptorSetLayouts
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy({ *get<0>(descriptorSetLayouts), *get<1>(descriptorSetLayouts), *get<2>(descriptorSetLayouts) }),
    } } { }