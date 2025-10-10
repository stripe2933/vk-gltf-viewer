module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.descriptor_set_layout.WeightedBlendedComposition;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct WeightedBlendedComposition : vku::raii::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eInputAttachment> {
        explicit WeightedBlendedComposition(const vk::raii::Device &device LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::WeightedBlendedComposition::WeightedBlendedComposition(const vk::raii::Device &device)
    : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::lvalue({
            DescriptorSetLayout::getCreateInfoBinding<0>(1, vk::ShaderStageFlagBits::eFragment),
            DescriptorSetLayout::getCreateInfoBinding<1>(1, vk::ShaderStageFlagBits::eFragment),
        }),
    } } { }