export module vk_gltf_viewer:vulkan.dsl.Scene;

import std;
import vku;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Scene : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer> {
        explicit Scene(const vk::raii::Device &device [[clang::lifetimebound]])
            : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eVertex })),
            } } { }
    };
}