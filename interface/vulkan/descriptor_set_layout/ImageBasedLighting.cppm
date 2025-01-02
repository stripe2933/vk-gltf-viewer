export module vk_gltf_viewer:vulkan.dsl.ImageBasedLighting;

import std;
import vku;
export import :vulkan.sampler.BrdfLutSampler;
export import :vulkan.sampler.CubemapSampler;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct ImageBasedLighting : vku::DescriptorSetLayout<vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eCombinedImageSampler> {
        ImageBasedLighting(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const CubemapSampler &cubemapSampler [[clang::lifetimebound]],
            const BrdfLutSampler &brdfLutSampler [[clang::lifetimebound]]
        ) : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::unsafeProxy(getBindings(
                    { 1, vk::ShaderStageFlagBits::eFragment },
                    { 1, vk::ShaderStageFlagBits::eFragment, &*cubemapSampler },
                    { 1, vk::ShaderStageFlagBits::eFragment, &*brdfLutSampler })),
            } } { }
    };
}