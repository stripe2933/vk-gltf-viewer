module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.descriptor_set_layout.Skybox;

import std;
import vku;

export import vk_gltf_viewer.vulkan.sampler.Cubemap;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Skybox : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler> {
        Skybox(const vk::raii::Device &device LIFETIMEBOUND, const sampler::Cubemap &cubemapSampler LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::Skybox::Skybox(
    const vk::raii::Device &device,
    const sampler::Cubemap &cubemapSampler
) : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eFragment, &*cubemapSampler })),
    } } { }