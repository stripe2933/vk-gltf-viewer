module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer.vulkan.mipmap;

import std;
export import vku;

import vk_gltf_viewer.helpers.ranges;

namespace vk_gltf_viewer::vulkan {
    /**
     * Record mipmap generation command for \p image to \p cb.
     * @tparam Dispatch Vulkan-Hpp dispatcher type.
     * @param cb Command buffer to be recorded. This should have graphics capability for blitting.
     * @param image Image proxy object that is pointing the to generate mipmap. It's usage must contain <tt>TransferSrc</tt> and <tt>TransferDst</tt>, and
     * layout must be <tt>TransferSrcOptimal</tt> for base mip level and <tt>TransferDstOptimal</tt> for the remaining mip levels.
     * @param d Vulkan-Hpp dispatcher.
     * @note The result image layout will be <tt>TransferSrcOptimal</tt> for all mip levels except for the last mip level, which will be <tt>TransferDstOptimal</tt>.
     * @note The last synchronization point will be image memory barrier, whose stage mask is <tt>Transfer</tt> and access mask is <tt>TransferWrite</tt>.
     * @note The image pointed by \p image must be alive until the command buffer is submitted and execution finished.
     * @see recordBatchedMipmapGenerationCommand for batched mipmap generation (efficient implementation for multiple images).
     */
    export template <typename Dispatch = VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>
    void recordMipmapGenerationCommand(vk::CommandBuffer cb, const vku::Image &image, Dispatch const & d VULKAN_HPP_DEFAULT_DISPATCHER_ASSIGNMENT) {
        for (std::uint32_t srcLevel = 0, dstLevel = 1; dstLevel < image.mipLevels; ++srcLevel, ++dstLevel) {
            // Blit from srcLevel to dstLevel.
            cb.blitImage(
                image, vk::ImageLayout::eTransferSrcOptimal,
                image, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageBlit {
                    { vk::ImageAspectFlagBits::eColor, srcLevel, 0, image.arrayLayers },
                    { vk::Offset3D{}, vku::toOffset3D(vku::mipExtent(image.extent, srcLevel)) },
                    { vk::ImageAspectFlagBits::eColor, dstLevel, 0, image.arrayLayers },
                    { vk::Offset3D{}, vku::toOffset3D(vku::mipExtent(image.extent, dstLevel)) },
                },
                vk::Filter::eLinear,
                d);

            // Barrier between each mip level.
            if (dstLevel != image.mipLevels - 1U) {
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                    {}, {}, {},
                    vk::ImageMemoryBarrier {
                        vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
                        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        image, { vk::ImageAspectFlagBits::eColor, dstLevel, 1, 0, vk::RemainingArrayLayers },
                    },
                    d);
            }
        }
    }

    export void recordMipmapGenerationCommand(const vk::raii::CommandBuffer &cb, const vku::Image &image) {
        recordMipmapGenerationCommand(cb, image, *cb.getDispatcher());
    }

    /**
     * Record batched mipmap generation command for \p images to \p cb. It efficiently generates blit commands between mip levels of multiple images and minimize pipeline barriers.
     * @tparam Dispatch Vulkan-Hpp dispatcher type.
     * @param cb Command buffer to be recorded. This should have graphics capability for blitting.
     * @param images Image proxy objects that are pointing the images to generate mipmap. Their usage must contain <tt>TransferSrc</tt> and <tt>TransferDst</tt>, and layout must be <tt>TransferSrcOptimal</tt> for base mip level and <tt>TransferDstOptimal</tt> for the remaining mip levels.
     * @param d Vulkan-Hpp dispatcher.
     * @note The result image layout will be <tt>TransferSrcOptimal</tt> for all mip levels except for the last mip level, which will be <tt>TransferDstOptimal</tt>.
     * @note The last synchronization point will be image memory barrier, whose stage mask is <tt>Transfer</tt> and access mask is <tt>TransferWrite</tt>.
     * @note All images pointed by \p images must be alive until the command buffer is submitted and execution finished.
     * @see recordMipmapGenerationCommand for single image mipmap generation.
     */
    export template <typename Dispatch = VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>
    void recordBatchedMipmapGenerationCommand(vk::CommandBuffer cb, ranges::random_access_range_of<vku::Image> auto images, Dispatch const & d VULKAN_HPP_DEFAULT_DISPATCHER_ASSIGNMENT) {
        // Sort image by their mip levels in ascending order.
        std::ranges::sort(images, {}, &vku::Image::mipLevels);

        std::vector imageMemoryBarriers(images.size(), vk::ImageMemoryBarrier {
            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            {}, // will be assigned in the below for loop
            { vk::ImageAspectFlagBits::eColor, 1 /* will be assigned in the below for loop */, 1, 0, 1 }
        });

        const std::uint32_t maxMipLevels = images.back().mipLevels;
        for (std::uint32_t srcLevel = 0, dstLevel = 1; dstLevel < maxMipLevels; ++srcLevel, ++dstLevel) {
            // Find the images that have the current mip level.
            auto imageIt = std::ranges::lower_bound(images, dstLevel + 1U, {}, &vku::Image::mipLevels);
            auto barrierIt = imageMemoryBarriers.begin();
            for (; imageIt != images.end(); ++imageIt, ++barrierIt) {
                // Blit from srcLevel to dstLevel.
                cb.blitImage(
                    *imageIt, vk::ImageLayout::eTransferSrcOptimal,
                    *imageIt, vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageBlit {
                        { vk::ImageAspectFlagBits::eColor, srcLevel, 0, imageIt->arrayLayers },
                        { vk::Offset3D{}, vku::toOffset3D(vku::mipExtent(imageIt->extent, srcLevel)) },
                        { vk::ImageAspectFlagBits::eColor, dstLevel, 0, imageIt->arrayLayers },
                        { vk::Offset3D{}, vku::toOffset3D(vku::mipExtent(imageIt->extent, dstLevel)) },
                    },
                    vk::Filter::eLinear,
                    d);

                barrierIt->image = *imageIt;
                barrierIt->subresourceRange.baseMipLevel = dstLevel;
            }

            // Barrier between each mip level.
            if (dstLevel != maxMipLevels - 1U) {
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                    {}, {}, {}, vku::lvalue(std::span { imageMemoryBarriers.begin(), barrierIt }),
                    d);
            }
        }
    }

    export void recordBatchedMipmapGenerationCommand(const vk::raii::CommandBuffer &cb, std::vector<vku::Image> images) {
        recordBatchedMipmapGenerationCommand(*cb, std::move(images), *cb.getDispatcher());
    }
}