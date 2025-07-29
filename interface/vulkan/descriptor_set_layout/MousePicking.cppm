module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.dsl.MousePicking;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct MousePicking : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer> {
        explicit MousePicking(const vk::raii::Device &device LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::MousePicking::MousePicking(const vk::raii::Device &device)
    : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
    } } { }