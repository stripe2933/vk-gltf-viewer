module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.descriptor_set_layout.Outline;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Outline : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> {
        explicit Outline(const vk::raii::Device &device LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::Outline::Outline(const vk::raii::Device &device)
    : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
    } } { }