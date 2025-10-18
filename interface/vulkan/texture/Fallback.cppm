module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.texture.Fallback;

import std;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::texture {
    /**
     * @brief Fallback image that would be used for texture-less material.
     *
     * If material is texture-less, only their factor is used. To match the shader code consistency, pipeline could
     * sample the white value and multiply it by factor to get the final color in the same ways as the texture is
     * presented.
     */
    export struct Fallback {
        /**
         * @brief 1x1 white image.
         */
        vku::raii::AllocatedImage image;

        /**
         * @brief Image view for <tt>image</tt>.
         */
        vk::raii::ImageView imageView;

        /**
         * @brief Sampler for fallback texture.
         */
        vk::raii::Sampler sampler;

        explicit Fallback(const Gpu &gpu LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::texture::Fallback::Fallback(const Gpu &gpu)
    : image {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eR8G8B8A8Unorm,
            { 1, 1, 1 },
            1, 1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        },
    }
    , imageView { gpu.device, image.getViewCreateInfo(vk::ImageViewType::e2D) }
    , sampler { gpu.device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        {}, {}, {},
        {},
        true, 16.f,
        {}, {},
        {}, vk::LodClampNone,
    } } {
    // Clear image as white.
    const vk::raii::CommandPool commandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
    const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, *commandPool, gpu.queues.graphicsPresent, [this](vk::CommandBuffer cb) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                {}, vk::AccessFlagBits::eTransferWrite,
                {}, vk::ImageLayout::eTransferDstOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor),
            });
        cb.clearColorImage(
            image, vk::ImageLayout::eTransferDstOptimal,
            vk::ClearColorValue { 1.f, 1.f, 1.f, 1.f },
            vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor));
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eTransferWrite, {},
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor),
            });
    }, *fence);
    // Wait for the command to be executed.
    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling
}