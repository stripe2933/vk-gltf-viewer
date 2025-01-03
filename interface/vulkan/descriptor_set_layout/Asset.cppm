export module vk_gltf_viewer:vulkan.dsl.Asset;

import std;
import vku;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Asset : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eCombinedImageSampler> {
        explicit Asset(const vk::raii::Device &device [[clang::lifetimebound]])
            : DescriptorSetLayout { device, vk::StructureChain {
                vk::DescriptorSetLayoutCreateInfo {
                    vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                    vku::unsafeProxy(getBindings(
                        { 1, vk::ShaderStageFlagBits::eVertex },
                        { 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },
                        { 126, vk::ShaderStageFlagBits::eFragment })),
                },
                vk::DescriptorSetLayoutBindingFlagsCreateInfo {
                    vku::unsafeProxy<vk::DescriptorBindingFlags>({
                        {},
                        {},
                        vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::eVariableDescriptorCount,
                    }),
                },
            }.get() } { }
    };
}