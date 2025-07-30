module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.texture.Textures;

import std;
export import BS.thread_pool;
export import fastgltf;
export import vkgltf;

export import vk_gltf_viewer.gltf.AssetExtended;
export import vk_gltf_viewer.gltf.AssetProcessError;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.vulkan;
import vk_gltf_viewer.vulkan.dsl.Asset;
import vk_gltf_viewer.vulkan.mipmap;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.texture.Fallback;

namespace vk_gltf_viewer::vulkan::texture {
    export struct Textures {
        std::vector<vk::raii::Sampler> samplers;
        std::unordered_map<std::size_t /* image index */, vkgltf::Image> images;

        std::vector<vk::DescriptorImageInfo> descriptorInfos;

        Textures(
            const gltf::AssetExtended &assetExtended,
            const Gpu &gpu LIFETIMEBOUND,
            const Fallback &fallbackTexture LIFETIMEBOUND,
            BS::thread_pool<> &threadPool
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::texture::Textures::Textures(
    const gltf::AssetExtended &assetExtended,
    const Gpu &gpu,
    const Fallback &fallbackTexture,
    BS::thread_pool<> &threadPool
) {
    if (1 + assetExtended.asset.textures.size() > dsl::Asset::maxTextureCount(gpu)) {
        // If asset texture count exceeds the available texture count provided by the GPU, throw the error before
        // processing data to avoid unnecessary processing.
        throw gltf::AssetProcessError::TooManyTextureError;
    }

    // ----- Images -----

    // Get images that are used by asset textures.
    std::vector usedImageIndices { std::from_range, assetExtended.asset.textures | std::views::transform(fastgltf::getPreferredImageIndex) };
    std::ranges::sort(usedImageIndices);
    const auto [begin, end] = std::ranges::unique(usedImageIndices);
    usedImageIndices.erase(begin, end);

    if (usedImageIndices.empty()) {
        // Nothing to do.
        return;
    }

    // Base color and emissive texture must be in SRGB format.
    // First traverse the asset textures and fetch the image index that must be in SRGB format.
    std::unordered_set<std::size_t> srgbImageIndices;
    for (const fastgltf::Material &material : assetExtended.asset.materials) {
        if (const auto &baseColorTexture = material.pbrData.baseColorTexture) {
            srgbImageIndices.emplace(getPreferredImageIndex(assetExtended.asset.textures[baseColorTexture->textureIndex]));
        }
        if (const auto &emissiveTexture = material.emissiveTexture) {
            srgbImageIndices.emplace(getPreferredImageIndex(assetExtended.asset.textures[emissiveTexture->textureIndex]));
        }
    }

    vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
    vkgltf::StagingBufferStorage stagingBufferStorage { gpu.device, transferCommandPool, gpu.queues.transfer };

    std::mutex mutex;
    vkgltf::StagingInfo stagingInfo {
        .stagingBufferStorage = stagingBufferStorage,
        .mutex = &mutex,
    };
    if (gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
        stagingInfo.queueFamilyOwnershipTransfer.emplace(gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent);
    }

    images.insert_range(threadPool.submit_sequence(0, usedImageIndices.size(), [&](std::size_t i) {
        const std::size_t imageIndex = usedImageIndices[i];

        const bool isSrgbImage = srgbImageIndices.contains(imageIndex);
        // If VK_KHR_swapchain_mutable_format is supported, ImGui will be rendered to B8G8R8A8Unorm swapchain image.
        // Therefore, sRGB asset images should be sampled as non-sRGB to prevent the color space mismatch.
        const bool allowMutateSrgbFormat = gpu.supportSwapchainMutableFormat == isSrgbImage;

        const vkgltf::Image::Config config {
            .adapter = assetExtended.externalBuffers,
            .allowMutateSrgbFormat = allowMutateSrgbFormat,
            .uncompressedImageFormatFn = [&](int channels) -> vk::Format {
                if (isSrgbImage) {
                    if (channels == 1 && gpu.supportR8SrgbImageFormat) {
                        return vk::Format::eR8Srgb;
                    }
                    if (channels == 2 && gpu.supportR8G8SrgbImageFormat) {
                        return vk::Format::eR8G8Srgb;
                    }
                    // Use 4-channel image for best compatibility.
                    return vk::Format::eR8G8B8A8Srgb;
                }
                else {
                    if (channels == 1 && (!allowMutateSrgbFormat || gpu.supportR8SrgbImageFormat)) {
                        return vk::Format::eR8Unorm;
                    }
                    if (channels == 2 && (!allowMutateSrgbFormat || gpu.supportR8G8SrgbImageFormat)) {
                        return vk::Format::eR8G8Unorm;
                    }

                    // Use 4-channel image for best compatibility.
                    return vk::Format::eR8G8B8A8Unorm;
                }
            },
            .uncompressedImageMipmapPolicy = vkgltf::Image::MipmapPolicy::AllocateOnly,
            .uncompressedImageUsageFlags = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
            .uncompressedImageDstLayout = vk::ImageLayout::eTransferSrcOptimal,
        #ifdef SUPPORT_KHR_TEXTURE_BASISU
            .compressedImageUsageFlags = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            .compressedImageDstLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        #endif
            .stagingInfo = &stagingInfo,
        };
        return std::pair<std::size_t, vkgltf::Image> {
            std::piecewise_construct,
            std::tuple { imageIndex },
            std::tie(assetExtended.asset, assetExtended.asset.images[imageIndex], assetExtended.directory, gpu.device, gpu.allocator, config),
        };
    }).get() | std::views::as_rvalue);

    vk::raii::Semaphore copyFinishSemaphore { gpu.device, vk::SemaphoreCreateInfo{} };
    stagingBufferStorage.execute(*copyFinishSemaphore);

    // Generate image mipmaps using graphics queue.
    std::optional<vk::raii::CommandPool> graphicsCommandPool; // Only have value if GPU has dedicated transfer queue.
    vk::CommandBuffer graphicsCommandBuffer;
    if (gpu.queueFamilies.graphicsPresent != gpu.queueFamilies.transfer) {
        const auto &inner = graphicsCommandPool.emplace(gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent });
        graphicsCommandBuffer = (*gpu.device).allocateCommandBuffers({ *inner, vk::CommandBufferLevel::ePrimary, 1 })[0];
    }
    else {
        graphicsCommandBuffer = (*gpu.device).allocateCommandBuffers({ *transferCommandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];
    }

    constexpr vk::PipelineStageFlags2 dependencyChain = vk::PipelineStageFlagBits2::eCopy;

    // Command buffer recording
    {
        graphicsCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        // Change image layouts and acquire resource queue family ownerships (optionally).
        std::vector<const vku::Image*> imagesToGenerateMipmap;
        std::vector<vk::ImageMemoryBarrier2> imageMemoryBarriers;
        for (const auto &[image, _] : images | std::views::values) {
        #ifdef SUPPORT_KHR_TEXTURE_BASISU
            if (isCompressed(image.format)) {
                if (stagingInfo.queueFamilyOwnershipTransfer) {
                    // Change the image layout of the copied region from TransferDstOptimal to ShaderReadOnlyOptimal, and do
                    // queue family ownership acquirement if GPU has dedicated transfer queue.
                    const auto &[src, dst] = *stagingInfo.queueFamilyOwnershipTransfer;
                    imageMemoryBarriers.push_back({
                        {}, {},
                        vk::PipelineStageFlagBits2::eAllCommands, {},
                        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                        src, dst,
                        image, vku::fullSubresourceRange(),
                    });
                }
            }
            else
        #endif
            {
                if (stagingInfo.queueFamilyOwnershipTransfer) {
                    const auto& [src, dst] = *stagingInfo.queueFamilyOwnershipTransfer;
                    if (image.mipLevels == 1) {
                        // Change the image layout from TransferDstOptimal to TransferSrcOptimal, and acquire queue family
                        // ownership if needed.
                        imageMemoryBarriers.push_back({
                            dependencyChain, {},
                            vk::PipelineStageFlagBits2::eAllCommands, {}, // dependency chain (A)
                            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                            src, dst,
                            image, vku::fullSubresourceRange(),
                        });
                    }
                    else {
                        // Change the image layout of the first mip region from TransferDstOptimal to TransferSrcOptimal,
                        // and acquire queue family ownership if needed.
                        imageMemoryBarriers.push_back({
                            dependencyChain, {},
                            vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
                            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                            src, dst,
                            image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                        });
                    }
                }

                if (image.mipLevels == 1) {
                    // Change the image layout from TransfeSrcOptimal to ShaderReadOnlyOptimal.
                    imageMemoryBarriers.push_back({
                        vk::PipelineStageFlagBits2::eAllCommands, {}, // dependency chain (A)
                        vk::PipelineStageFlagBits2::eAllCommands, {},
                        vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        image, vku::fullSubresourceRange(),
                    });
                }
                else {
                    // Change the image layout of the mipmap region to TransferDstOptimal.
                    imageMemoryBarriers.push_back({
                        {}, {},
                        vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
                        {}, vk::ImageLayout::eTransferDstOptimal,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        image, { vk::ImageAspectFlagBits::eColor, 1, vk::RemainingArrayLayers, 0, 1 },
                    });

                    imagesToGenerateMipmap.push_back(&image);
                }
            }
        }
        graphicsCommandBuffer.pipelineBarrier2KHR({ {}, {}, {}, imageMemoryBarriers });

        if (!imagesToGenerateMipmap.empty()) {
            recordBatchedMipmapGenerationCommand(graphicsCommandBuffer, imagesToGenerateMipmap);

            // Change the image layout to ShaderReadOnlyOptimal.
            std::vector<vk::ImageMemoryBarrier> imageMemoryBarriersToBottom;
            imageMemoryBarriersToBottom.reserve(2 * imagesToGenerateMipmap.size());
            for (const vku::Image *image : imagesToGenerateMipmap) {
                imageMemoryBarriersToBottom.push_back({
                    vk::AccessFlagBits::eTransferRead, {},
                    vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    *image, { vk::ImageAspectFlagBits::eColor, 0, image->mipLevels - 1, 0, 1 },
                });
                imageMemoryBarriersToBottom.push_back({
                    vk::AccessFlagBits::eTransferWrite, {},
                    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    *image, { vk::ImageAspectFlagBits::eColor, image->mipLevels - 1, 1, 0, 1 },
                });
            }

            graphicsCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
                {}, {}, {}, imageMemoryBarriersToBottom);
        }

        graphicsCommandBuffer.end();
    }

    vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    gpu.queues.graphicsPresent.submit2KHR(vk::SubmitInfo2 {
        {},
        vku::unsafeProxy(vk::SemaphoreSubmitInfo { *copyFinishSemaphore, {}, dependencyChain }),
        vku::unsafeProxy(vk::CommandBufferSubmitInfo { graphicsCommandBuffer }),
    }, *fence);

    // Do some nice things during the GPU execution.
    {
        // samplers
        samplers.reserve(assetExtended.asset.samplers.size());
        for (const fastgltf::Sampler &sampler : assetExtended.asset.samplers) {
            samplers.emplace_back(gpu.device, vkgltf::getSamplerCreateInfo(sampler, 16.f));
        }

        // descriptorInfos
        descriptorInfos.reserve(assetExtended.asset.textures.size());
        for (const fastgltf::Texture &texture : assetExtended.asset.textures) {
            vk::Sampler sampler = *fallbackTexture.sampler;
            if (texture.samplerIndex) {
                sampler = *samplers[*texture.samplerIndex];
            }

            descriptorInfos.emplace_back(sampler, *images.at(getPreferredImageIndex(texture)).view, vk::ImageLayout::eShaderReadOnlyOptimal);
        }
    }

    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL);

    // Destroy staging buffers.
    stagingBufferStorage.reset(false /* stagingBufferStorage will not be used anymore */);
}