export module vk_gltf_viewer:vulkan.dsl.Asset;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Asset : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eCombinedImageSampler> {
        Asset(const vk::raii::Device &device [[clang::lifetimebound]], std::uint32_t textureCount)
            : DescriptorSetLayout { device, vk::StructureChain {
                vk::DescriptorSetLayoutCreateInfo {
                    vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                    vku::unsafeProxy(getBindings(
                        { 1, vk::ShaderStageFlagBits::eVertex },
                        { 1, vk::ShaderStageFlagBits::eVertex },
                        { 1, vk::ShaderStageFlagBits::eVertex },
                        { 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },
                        { textureCount, vk::ShaderStageFlagBits::eFragment })),
                },
                vk::DescriptorSetLayoutBindingFlagsCreateInfo {
                    vku::unsafeProxy<vk::DescriptorBindingFlags>({
                        {},
                        {},
                        {},
                        {},
                        vk::DescriptorBindingFlagBits::eUpdateAfterBind,
                    }),
                },
            }.get() } { }
    };
}