module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.descriptor_set_layout.Skybox;

#ifdef _MSC_VER
import std;
#endif
export import vku;

export import vk_gltf_viewer.vulkan.sampler.Cubemap;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Skybox final : vku::raii::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler> {
        explicit Skybox(const vk::raii::Device &device LIFETIMEBOUND, const sampler::Cubemap &cubemapSampler LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::Skybox::Skybox(const vk::raii::Device &device, const sampler::Cubemap &cubemapSampler)
    : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::lvalue(DescriptorSetLayout::getCreateInfoBinding<0>(vk::ShaderStageFlagBits::eFragment, *cubemapSampler)),
    } } { }