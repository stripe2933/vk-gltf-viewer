export module vk_gltf_viewer:vulkan.dsl.Asset;

import std;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct Asset : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eCombinedImageSampler> {
        explicit Asset(const Gpu &gpu [[clang::lifetimebound]])
            : DescriptorSetLayout { gpu.device, vk::StructureChain {
                vk::DescriptorSetLayoutCreateInfo {
                    vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                    vku::unsafeProxy(getBindings(
                        { 1, vk::ShaderStageFlagBits::eVertex },
                        { 1, vk::ShaderStageFlagBits::eVertex },
                        { 1, vk::ShaderStageFlagBits::eVertex },
                        { 1, vk::ShaderStageFlagBits::eVertex },
                        { 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },
                        { maxTextureCount(gpu), vk::ShaderStageFlagBits::eFragment })),
                },
                vk::DescriptorSetLayoutBindingFlagsCreateInfo {
                    vku::unsafeProxy<vk::DescriptorBindingFlags>({
                        {},
                        {},
                        {},
                        {},
                        {},
                        vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::eVariableDescriptorCount,
                    }),
                },
            }.get() } { }

        Asset(const Gpu &gpu [[clang::lifetimebound]], std::uint32_t textureCount)
            : DescriptorSetLayout { gpu.device, vk::StructureChain {
                vk::DescriptorSetLayoutCreateInfo {
                    vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                    vku::unsafeProxy(getBindings(
                        { 1, vk::ShaderStageFlagBits::eVertex },
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
                        {},
                        vk::DescriptorBindingFlagBits::eUpdateAfterBind,
                    }),
                },
            }.get() } { }

        /**
         * Get maximum available texture count for asset, including the fallback texture.
         * @param gpu The GPU object that is storing <tt>maxPerStageDescriptorUpdateAfterBindSamplers</tt> which have been retrieved from physical device selection.
         * @return The maximum available texture count.
         */
        [[nodiscard]] static std::uint32_t maxTextureCount(const Gpu &gpu) noexcept {
            // BRDF LUT texture and prefiltered map texture will acquire two slots, therefore we need to subtract 2.
            return std::min(gpu.maxPerStageDescriptorUpdateAfterBindSamplers, 128U) - 2U;
        }
    };
}