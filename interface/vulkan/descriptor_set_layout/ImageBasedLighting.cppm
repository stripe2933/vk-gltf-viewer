export module vk_gltf_viewer:vulkan.dsl.ImageBasedLighting;

import std;
import vku;
export import :vulkan.sampler.BrdfLutSampler;
export import :vulkan.sampler.CubemapSampler;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct ImageBasedLighting : vku::DescriptorSetLayout<vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eCombinedImageSampler> {
        ImageBasedLighting(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const BrdfLutSampler &brdfLutSampler [[clang::lifetimebound]],
            const CubemapSampler &cubemapSampler [[clang::lifetimebound]]
        ) : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment },
                    vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &*brdfLutSampler },
                    vk::DescriptorSetLayoutBinding { 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &*cubemapSampler },
                }),
            } } { }
    };
}