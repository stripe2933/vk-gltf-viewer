export module vk_gltf_viewer:vulkan.dsl.Skybox;

import std;
import vku;
export import :vulkan.sampler.CubemapSampler;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Skybox : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler> {
        Skybox(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const CubemapSampler &sampler [[clang::lifetimebound]]
        ) : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &*sampler },
                }),
            } } { }
    };
}