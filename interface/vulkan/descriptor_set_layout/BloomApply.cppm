module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.descriptor_set_layout.BloomApply;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct BloomApply final : vku::DescriptorSetLayout<vk::DescriptorType::eInputAttachment, vk::DescriptorType::eSampledImage> {
        explicit BloomApply(const vk::raii::Device &device LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::BloomApply::BloomApply(const vk::raii::Device &device)
    : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(getBindings(
            { 1, vk::ShaderStageFlagBits::eFragment },
            { 1, vk::ShaderStageFlagBits::eFragment })),
    } } { }