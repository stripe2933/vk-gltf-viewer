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
import vk_gltf_viewer.vulkan.descriptor_set_layout.Asset;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.texture.Fallback;

#if __APPLE__
import MetalCpp;
import ObjCBridge;

import vk_gltf_viewer.helpers.ranges;
#else
import vk_gltf_viewer.vulkan.mipmap;
#endif

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

#if !__APPLE__
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
#endif

    images.insert_range(threadPool.submit_sequence(0, usedImageIndices.size(), [&](std::size_t i) {
        const std::size_t imageIndex = usedImageIndices[i];

        const bool isSrgbImage = srgbImageIndices.contains(imageIndex);
        // If VK_KHR_swapchain_mutable_format is supported, ImGui will be rendered to B8G8R8A8Unorm swapchain image.
        // Therefore, sRGB asset images should be sampled as non-sRGB to prevent the color space mismatch.
        const bool allowMutateSrgbFormat = gpu.supportSwapchainMutableFormat == isSrgbImage;

        const vkgltf::Image::Config config {
            .adapter = assetExtended.externalBuffers,
            .allowMutateSrgbFormat = allowMutateSrgbFormat,
        #if __APPLE__
            .imageCopyDstLayout = vk::ImageLayout::eShaderReadOnlyOptimal, // Prevent double layout transition
        #endif
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
        #if __APPLE__
            .metalObjectTypeFlagBits = vk::ExportMetalObjectTypeFlagBitsEXT::eMetalTexture,
        #else
            .uncompressedImageUsageFlags = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
            .uncompressedImageDstLayout = vk::ImageLayout::eTransferSrcOptimal,
        #ifdef SUPPORT_KHR_TEXTURE_BASISU
            .compressedImageUsageFlags = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        #endif
            .stagingInfo = &stagingInfo,
        #endif
        };
        return std::pair<std::size_t, vkgltf::Image> {
            std::piecewise_construct,
            std::tuple { imageIndex },
            std::tie(assetExtended.asset, assetExtended.asset.images[imageIndex], assetExtended.directory, gpu.device, gpu.allocator, config),
        };
    }).get() | std::views::as_rvalue);

#if !__APPLE__
    vk::raii::Semaphore copyFinishSemaphore { gpu.device, vk::SemaphoreCreateInfo{} };
    stagingBufferStorage.execute(*copyFinishSemaphore);
#endif

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

#if __APPLE__
    std::vector<vk::ExportMetalTextureInfoEXT> exportTextureInfos;
    exportTextureInfos.reserve(images.size());
    std::unordered_set<vk::Image> imagesToGenerateMipmap;

    for (const auto &[image, _] : images | std::views::values) {
        exportTextureInfos.push_back({ image, {}, {}, vk::ImageAspectFlagBits::ePlane0 });
        if (!isCompressed(image.format) && image.mipLevels > 1) {
            imagesToGenerateMipmap.emplace(image);
        }
    }
    for (const auto &[a, b] : exportTextureInfos | ranges::views::pairwise) {
        a.pNext = &b;
    }

    // Export MTLCommandQueue (from graphics queue) and MTLTextures (from generated images).
    vk::StructureChain exportInfos {
        vk::ExportMetalObjectsInfoEXT{},
        vk::ExportMetalCommandQueueInfoEXT { gpu.queues.graphicsPresent }
            .setPNext(&exportTextureInfos.front()),
    };
    gpu.device.exportMetalObjectsEXT(exportInfos.get());

    MTL::CommandQueue* const commandQueue = (MTL::CommandQueue*)ObjCBridge_bridge(exportInfos.get<vk::ExportMetalCommandQueueInfoEXT>().mtlCommandQueue);
    MTL::CommandBuffer* const commandBuffer = commandQueue->commandBufferWithUnretainedReferences();

    MTL::BlitCommandEncoder* const blitCommandEncoder = commandBuffer->blitCommandEncoder();
    for (const vk::ExportMetalTextureInfoEXT &exportTextureInfo : exportTextureInfos) {
        MTL::Texture* const texture = (MTL::Texture*)ObjCBridge_bridge(exportTextureInfo.mtlTexture);
        if (imagesToGenerateMipmap.contains(exportTextureInfo.image)) {
            blitCommandEncoder->generateMipmaps(texture);
        }
        blitCommandEncoder->optimizeContentsForGPUAccess(texture);
    }
    blitCommandEncoder->endEncoding();

    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();

    if (!imagesToGenerateMipmap.empty()) {
        // Prevent Validation Layer complains.
        gpu.device.transitionImageLayoutEXT(
            imagesToGenerateMipmap
                | std::views::transform([](vk::Image image) noexcept {
                    return vk::HostImageLayoutTransitionInfo { image, {}, vk::ImageLayout::eShaderReadOnlyOptimal, vku::fullSubresourceRange() };
                })
                | std::ranges::to<std::vector>());
    }
#else
    // Graphics capable queue is needed for vkCmdBlitImage().
    std::optional<vk::raii::CommandPool> graphicsCommandPool; // Only will have value if GPU has dedicated transfer queue.
    vk::CommandBuffer graphicsCommandBuffer;
    if (gpu.queueFamilies.graphicsPresent == gpu.queueFamilies.transfer) {
        graphicsCommandBuffer = (*gpu.device).allocateCommandBuffers({ *transferCommandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];
    }
    else
    {
        const auto &inner = graphicsCommandPool.emplace(gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent });
        graphicsCommandBuffer = (*gpu.device).allocateCommandBuffers({ *inner, vk::CommandBufferLevel::ePrimary, 1 })[0];
    }

    constexpr vk::PipelineStageFlags2 dependencyChain = vk::PipelineStageFlagBits2::eCopy;

    // Command buffer recording
    {
        graphicsCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        // Change image layouts and acquire resource queue family ownerships (optionally).
        std::vector<vk::ImageMemoryBarrier2> imageMemoryBarriers;
        std::vector<const vku::Image*> imagesToGenerateMipmap;
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
                    // Change the image layout from TransferSrcOptimal to ShaderReadOnlyOptimal.
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

                    // Image is needed to generate mipmap.
                    imagesToGenerateMipmap.push_back(&image);
                }
            }
        }
        graphicsCommandBuffer.pipelineBarrier2KHR({ {}, {}, {}, imageMemoryBarriers });

        if (!imagesToGenerateMipmap.empty()) {
            // Collect image memory barriers that are inserted after the mipmap generation command.
            // Note: recordBatchedMipmapGenerationCommand() takes ownership of std::vector<const vku::Image*>, therefore
            // the vector should be moved. But the vector is also need for collecting barriers, therefore this code is
            // intentionally in here (instead of after recordBatchedMipmapGenerationCommand()).
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

            // Generate mipmaps.
            recordBatchedMipmapGenerationCommand(graphicsCommandBuffer, std::move(imagesToGenerateMipmap));

            // Change the image layout to ShaderReadOnlyOptimal.
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

    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL);

    // Destroy staging buffers.
    stagingBufferStorage.reset(false /* stagingBufferStorage will not be used anymore */);
#endif
}