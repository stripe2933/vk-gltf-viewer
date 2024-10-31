export module vk_gltf_viewer:vulkan.dsl.Asset;

import std;
import vku;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Asset : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer> {
        Asset(const vk::raii::Device &device [[clang::lifetimebound]], std::uint32_t textureCount)
            : DescriptorSetLayout { device, vk::StructureChain {
                vk::DescriptorSetLayoutCreateInfo {
                    vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                    vku::unsafeProxy({
                        vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, textureCount, vk::ShaderStageFlagBits::eFragment },
                        vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },
                        vk::DescriptorSetLayoutBinding { 2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
                    }),
                },
                vk::DescriptorSetLayoutBindingFlagsCreateInfo {
                    vku::unsafeProxy({
                        vk::Flags { vk::DescriptorBindingFlagBits::eUpdateAfterBind },
                        vk::DescriptorBindingFlags{},
                        vk::DescriptorBindingFlags{},
                    }),
                },
            }.get() } { }
    };
}