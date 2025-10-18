module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.descriptor_set_layout.InverseToneMapping;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct InverseToneMapping final : vku::raii::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eStorageImage> {
        explicit InverseToneMapping(const vk::raii::Device &device LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::InverseToneMapping::InverseToneMapping(const vk::raii::Device &device)
    : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::lvalue({
            DescriptorSetLayout::getCreateInfoBinding<0>(1, vk::ShaderStageFlagBits::eFragment),
            DescriptorSetLayout::getCreateInfoBinding<1>(1, vk::ShaderStageFlagBits::eFragment),
        }),
    } } { }