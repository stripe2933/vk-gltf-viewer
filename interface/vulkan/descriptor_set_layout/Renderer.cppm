module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.dsl.Renderer;

import std;
import vku;

export import vk_gltf_viewer.vulkan.sampler.BrdfLut;
export import vk_gltf_viewer.vulkan.sampler.Cubemap;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Renderer final : vku::DescriptorSetLayout<vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eCombinedImageSampler> {
        Renderer(
            const vk::raii::Device &device LIFETIMEBOUND,
            const sampler::Cubemap &cubemapSampler LIFETIMEBOUND,
            const sampler::BrdfLut &brdfLutSampler LIFETIMEBOUND
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::Renderer::Renderer(
    const vk::raii::Device &device,
    const sampler::Cubemap &cubemapSampler,
    const sampler::BrdfLut &brdfLutSampler
) : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(getBindings(
            { 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },
            { 1, vk::ShaderStageFlagBits::eFragment },
            { 1, vk::ShaderStageFlagBits::eFragment, &*cubemapSampler },
            { 1, vk::ShaderStageFlagBits::eFragment, &*brdfLutSampler })),
    } } { }