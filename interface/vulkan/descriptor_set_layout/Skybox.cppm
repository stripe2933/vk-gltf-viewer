export module vk_gltf_viewer:vulkan.dsl.Skybox;

import std;
import vku;
export import :vulkan.sampler.Cubemap;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Skybox : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler> {
        Skybox(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const sampler::Cubemap &cubemapSampler [[clang::lifetimebound]]
        ) : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eFragment, &*cubemapSampler })),
            } } { }
    };
}