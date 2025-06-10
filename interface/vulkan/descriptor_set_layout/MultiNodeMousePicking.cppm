module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.dsl.MultiNodeMousePicking;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct MultiNodeMousePicking : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer> {
        explicit MultiNodeMousePicking(const vk::raii::Device &device LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::MultiNodeMousePicking::MultiNodeMousePicking(const vk::raii::Device &device)
    : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
    } } { }