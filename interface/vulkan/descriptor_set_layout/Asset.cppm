module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.descriptor_set_layout.Asset;

import std;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::dsl {
#if __APPLE__
    // Metal does not have a concept of combined image sampler, so MoltenVK mimics it by mixing them. This makes the
    // available texture count tied to VkPhysicalDeviceDescriptorIndexingProperties::maxPerStageDescriptorUpdateAfterBindSamplers,
    // which is very small (16) when not using Metal Argument Buffer. It is also buggy when using Metal Argument Buffer,
    // due to the poor implementation.
    //
    // For workaround, we'll manually separate the sampler and image (see SEPARATE_IMAGE_SAMPLER macro definition in the
    // primitive rendering pipeline's fragment shader).
    export struct Asset : vku::raii::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eSampler, vk::DescriptorType::eSampledImage> {
#else
    export struct Asset : vku::raii::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eCombinedImageSampler> {
#endif
        explicit Asset(const Gpu &gpu LIFETIMEBOUND);

        /**
         * @brief Get maximum available sampler (in Apple system) or texture (otherwise) count for asset, including the fallback texture.
         *
         * @param gpu The GPU object that is storing <tt>maxPerStageDescriptorUpdateAfterBindSamplers</tt> which have been retrieved from physical device selection.
         * @return The maximum available sampler count.
         */
        [[nodiscard]] static std::uint32_t maxSamplerCount(const Gpu &gpu) noexcept;

#if __APPLE__
        /**
         * @brief Get maximum available image count for asset, including the fallback texture.
         *
         * @param gpu The GPU object that is storing <tt>maxPerStageDescriptorUpdateAfterBindSampledImages</tt> which have been retrieved from physical device selection.
         * @return The maximum available image count.
         */
        [[nodiscard]] static std::uint32_t maxImageCount(const Gpu &gpu) noexcept;
#endif
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::dsl::Asset::Asset(const Gpu &gpu)
    : DescriptorSetLayout { gpu.device, vk::StructureChain {
        vk::DescriptorSetLayoutCreateInfo {
            vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
            vku::lvalue({
                DescriptorSetLayout::getCreateInfoBinding<0>(1, vk::ShaderStageFlagBits::eVertex),
                DescriptorSetLayout::getCreateInfoBinding<1>(1, vk::ShaderStageFlagBits::eVertex),
                DescriptorSetLayout::getCreateInfoBinding<2>(1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
            #if __APPLE__
                DescriptorSetLayout::getCreateInfoBinding<3>(maxSamplerCount(gpu), vk::ShaderStageFlagBits::eFragment),
                DescriptorSetLayout::getCreateInfoBinding<4>(maxImageCount(gpu), vk::ShaderStageFlagBits::eFragment),
            #else
                DescriptorSetLayout::getCreateInfoBinding<3>(maxSamplerCount(gpu), vk::ShaderStageFlagBits::eFragment),
            #endif
            }),
        },
        vk::DescriptorSetLayoutBindingFlagsCreateInfo {
            vku::lvalue<vk::DescriptorBindingFlags>({
                {},
                {},
                {},
            #if __APPLE__
                vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound,
                vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount,
            #else
                vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::eVariableDescriptorCount,
            #endif
            }),
        },
    }.get() } { }

std::uint32_t vk_gltf_viewer::vulkan::dsl::Asset::maxSamplerCount(const Gpu &gpu) noexcept {
    constexpr std::uint32_t limit
    #if __APPLE__
        = 16U; // We don't need that many samplers.
    #else
        = 512U;
    #endif

    // BRDF LUT texture and prefiltered map texture will acquire two slots, therefore we need to subtract 2.
    return std::min(gpu.maxPerStageDescriptorUpdateAfterBindSamplers, limit) - 2U;
}

#if __APPLE__
std::uint32_t vk_gltf_viewer::vulkan::dsl::Asset::maxImageCount(const Gpu& gpu) noexcept {
    // BRDF LUT texture and prefiltered map texture will acquire two slots, therefore we need to subtract 2.
    return std::min(gpu.maxPerStageDescriptorUpdateAfterBindSampledImages, 512U) - 2U;
}
#endif