module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.mipmap;

import std;
export import vku;
import :helpers.ranges;

namespace vk_gltf_viewer::vulkan {
    /**
     * Record mipmap generation command for \p image to \p cb.
     *
     * If \p useGeneralImageLayout is <tt>true</tt>,
     * - \p image layout must be <tt>vk::ImageLayout::eGeneral</tt>.
     * - Result \p image layout will be <tt>vk::ImageLayout::eGeneral</tt>.
     *
     * If \p useGeneralImageLayout is <tt>false</tt>,
     * - \p image layout must be <tt>TransferSrcOptimal</tt> for base mip level and <tt>TransferDstOptimal</tt> for the remaining mip levels.
     * - Result \p image layout will be <tt>TransferSrcOptimal</tt> for all mip levels except for the last mip level, which will be <tt>TransferDstOptimal</tt>.
     *
     * @param cb Command buffer to be recorded. This should have graphics capability for blitting.
     * @param image Image to generate mipmap. It's usage must contain <tt>TransferSrc</tt> and <tt>TransferDst</tt>.
     * @param useGeneralImageLayout See explanation for detail.
     * @note The last synchronization point will be image memory barrier, whose stage mask is <tt>Transfer</tt> and access mask is <tt>TransferWrite</tt>.
     * @note \p image must be alive until the command buffer is submitted and execution finished.
     * @see recordBatchedMipmapGenerationCommand for batched mipmap generation (efficient implementation for multiple images).
     */
    export void recordMipmapGenerationCommand(vk::CommandBuffer cb, const vku::Image &image, bool useGeneralImageLayout = false) {
        for (std::uint32_t srcLevel = 0, dstLevel = 1; dstLevel < image.mipLevels; ++srcLevel, ++dstLevel) {
            // Blit from srcLevel to dstLevel.
            cb.blitImage(
                image, useGeneralImageLayout ? vk::ImageLayout::eGeneral : vk::ImageLayout::eTransferSrcOptimal,
                image, useGeneralImageLayout ? vk::ImageLayout::eGeneral : vk::ImageLayout::eTransferDstOptimal,
                vk::ImageBlit {
                    { vk::ImageAspectFlagBits::eColor, srcLevel, 0, 1 },
                    { vk::Offset3D{}, vku::toOffset3D(image.mipExtent(srcLevel)) },
                    { vk::ImageAspectFlagBits::eColor, dstLevel, 0, 1 },
                    { vk::Offset3D{}, vku::toOffset3D(image.mipExtent(dstLevel)) },
                },
                vk::Filter::eLinear);

            // Barrier between each mip level.
            if (dstLevel != image.mipLevels - 1U) {
                if (useGeneralImageLayout) {
                    cb.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                        {},
                        vk::MemoryBarrier {
                            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
                        }, {}, {});
                }
                else {
                    cb.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                        {}, {}, {},
                        vk::ImageMemoryBarrier {
                            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
                            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                            image, { vk::ImageAspectFlagBits::eColor, dstLevel, 1, 0, 1 }
                        });
                }
            }
        }
    }

    /**
     * Record batched mipmap generation command for \p images to \p cb. It efficiently generates blit commands between mip levels of multiple images and minimize pipeline barriers.
     *
     * If \p useGeneralImageLayout is <tt>true</tt>,
     * - \p images layout must be <tt>vk::ImageLayout::eGeneral</tt>.
     * - Result \p images layout will be <tt>vk::ImageLayout::eGeneral</tt>.
     *
     * If \p useGeneralImageLayout is <tt>false</tt>,
     * - \p images layout must be <tt>TransferSrcOptimal</tt> for base mip level and <tt>TransferDstOptimal</tt> for the remaining mip levels.
     * - Result \p images layout will be <tt>TransferSrcOptimal</tt> for all mip levels except for the last mip level, which will be <tt>TransferDstOptimal</tt>.
     *
     * @param cb Command buffer to be recorded. This should have graphics capability for blitting.
     * @param images Images to generate mipmap. Their usage must contain <tt>TransferSrc</tt> and <tt>TransferDst</tt>.
     * @param useGeneralImageLayout See explanation for detail.
     * @note The result image layout will be <tt>TransferSrcOptimal</tt> for all mip levels except for the last mip level, which will be <tt>TransferDstOptimal</tt>.
     * @note The last synchronization point will be image memory barrier, whose stage mask is <tt>Transfer</tt> and access mask is <tt>TransferWrite</tt>.
     * @note \p images must be alive until the command buffer is submitted and execution finished.
     * @see recordMipmapGenerationCommand for single image mipmap generation.
     */
    export template <std::ranges::forward_range R>
        requires std::derived_from<std::ranges::range_value_t<R>, vku::Image>
    void recordBatchedMipmapGenerationCommand(vk::CommandBuffer cb, R &&images, bool useGeneralImageLayout = false) {
        // 1. Sort image by their mip levels in ascending order.
        std::vector pImages
            = images
            | std::views::transform([](const vku::Image &image) { return &image; })
            | std::ranges::to<std::vector>();
        std::ranges::sort(pImages, {}, [](const vku::Image *pImage) { return pImage->mipLevels; });

        const std::uint32_t maxMipLevels = pImages.back()->mipLevels;
        for (std::uint32_t srcLevel = 0, dstLevel = 1; dstLevel < maxMipLevels; ++srcLevel, ++dstLevel) {
            // Find the images that have the current mip level.
            auto begin = std::ranges::lower_bound(
                pImages, dstLevel + 1U, {}, [](const vku::Image *pImage) { return pImage->mipLevels; });
            const auto targetImages = std::ranges::subrange(begin, pImages.end()) | ranges::views::deref;

            // Blit from srcLevel to dstLevel.
            for (const vku::Image &image : targetImages) {
                cb.blitImage(
                    image, useGeneralImageLayout ? vk::ImageLayout::eGeneral : vk::ImageLayout::eTransferSrcOptimal,
                    image, useGeneralImageLayout ? vk::ImageLayout::eGeneral : vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageBlit {
                        { vk::ImageAspectFlagBits::eColor, srcLevel, 0, 1 },
                        { vk::Offset3D{}, vku::toOffset3D(image.mipExtent(srcLevel)) },
                        { vk::ImageAspectFlagBits::eColor, dstLevel, 0, 1 },
                        { vk::Offset3D{}, vku::toOffset3D(image.mipExtent(dstLevel)) },
                    },
                    vk::Filter::eLinear);
            }

            // Barrier between each mip level.
            if (dstLevel != maxMipLevels - 1U) {
                if (useGeneralImageLayout) {
                    cb.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                        {},
                        vk::MemoryBarrier {
                            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
                        }, {}, {});
                }
                else {
                    cb.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                        {}, {}, {},
                        vku::unsafeProxy(targetImages | std::views::transform([=](vk::Image image) {
                            return vk::ImageMemoryBarrier {
                                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
                                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                                image, { vk::ImageAspectFlagBits::eColor, dstLevel, 1, 0, 1 }
                            };
                        }) | std::ranges::to<std::vector>()));
                }
            }
        }
    }
}