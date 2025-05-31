module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.dsl.ImageBasedLighting;

import std;
import vku;

export import vk_gltf_viewer.vulkan.sampler.BrdfLut;
export import vk_gltf_viewer.vulkan.sampler.Cubemap;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct ImageBasedLighting : vku::DescriptorSetLayout<vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eCombinedImageSampler> {
        ImageBasedLighting(
            const vk::raii::Device &device LIFETIMEBOUND,
            const sampler::Cubemap &cubemapSampler LIFETIMEBOUND,
            const sampler::BrdfLut &brdfLutSampler LIFETIMEBOUND
        ) : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::unsafeProxy(getBindings(
                    { 1, vk::ShaderStageFlagBits::eFragment },
                    { 1, vk::ShaderStageFlagBits::eFragment, &*cubemapSampler },
                    { 1, vk::ShaderStageFlagBits::eFragment, &*brdfLutSampler })),
            } } { }
    };
}