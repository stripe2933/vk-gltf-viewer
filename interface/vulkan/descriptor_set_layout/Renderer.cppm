module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.descriptor_set_layout.Renderer;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Renderer final : vku::raii::DescriptorSetLayout<vk::DescriptorType::eUniformBuffer> {
        explicit Renderer(const vk::raii::Device &device LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::Renderer::Renderer(const vk::raii::Device &device)
    : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::lvalue(DescriptorSetLayout::getCreateInfoBinding<0>(1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)),
    } } { }