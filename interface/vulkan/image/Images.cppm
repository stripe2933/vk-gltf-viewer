module;

#ifdef SUPPORT_KHR_TEXTURE_BASISU
#include <ktx.h>
#endif
#include <stb_image.h>
#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer.vulkan.image.Images;

import std;
export import BS.thread_pool;
export import fastgltf;

export import vk_gltf_viewer.gltf.AssetProcessError;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.io;
import vk_gltf_viewer.helpers.ranges;
import vk_gltf_viewer.helpers.span;
import vk_gltf_viewer.helpers.vulkan;
import vk_gltf_viewer.vulkan.mipmap;
export import vk_gltf_viewer.vulkan.Gpu;

#ifdef _WIN32
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

namespace vk_gltf_viewer::vulkan::image {
    export struct Info {
        bool alphaChannelPadded;
    };

    /**
     * @brief GPU images (images and image views) for <tt>fastgltf::Asset</tt>.
     *
     * It loads the required images from <tt>fastgltf::Asset</tt> at the construction time with multithreaded image
     * decoding and resource creation.
     *
     * The term "required" means only the images that are used by rendering, which are base color, metallic,
     * roughness, normal, occlusion and emissive textures. Therefore, even if the texture is presented in the glTF asset,
     * it may not be presented in this class, which makes the type of the field <tt>images</tt> to be
     * <tt>std::unordered_map<std::size_t, vku::AllocatedImage></tt> instead of <tt>std::vector<vku::AllocatedImage></tt>
     * (also for <tt>imageViews</tt>).
     */
    export class Images : public std::unordered_map<std::size_t, std::tuple<vku::AllocatedImage, vk::raii::ImageView, Info>> {
        /**
         * Staging buffers for temporary data transfer. This have to be cleared after the transfer command execution
         * finished.
         */
        std::forward_list<vku::AllocatedBuffer> stagingBuffers;

    public:
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        Images(
            const fastgltf::Asset &asset,
            const std::filesystem::path &assetDir,
            const Gpu &gpu,
            BS::thread_pool<> &threadPool,
            const BufferDataAdapter &adapter = {}
        ) {
            // Get images that are used by asset textures.
            std::vector usedImageIndices { std::from_range, asset.textures | std::views::transform(fastgltf::getPreferredImageIndex) };
            std::ranges::sort(usedImageIndices);
            const auto [begin, end] = std::ranges::unique(usedImageIndices);
            usedImageIndices.erase(begin, end);

            if (usedImageIndices.empty()) {
                // Nothing to do.
                return;
            }

            // Image indices whose mipmap have to be manually generated using blit chain.
            std::unordered_set<std::size_t> imageIndicesToGenerateMipmap;

            // Transfer the asset resources into the GPU using transfer queue.
            const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
            const vk::raii::Fence transferFence { gpu.device, vk::FenceCreateInfo{} };
            vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
                // Base color and emissive texture must be in SRGB format.
                // First traverse the asset textures and fetch the image index that must be in SRGB format.
                std::unordered_set<std::size_t> srgbImageIndices;
                for (const fastgltf::Material &material : asset.materials) {
                    if (const auto &baseColorTexture = material.pbrData.baseColorTexture) {
                        srgbImageIndices.emplace(getPreferredImageIndex(asset.textures[baseColorTexture->textureIndex]));
                    }
                    if (const auto &emissiveTexture = material.emissiveTexture) {
                        srgbImageIndices.emplace(getPreferredImageIndex(asset.textures[emissiveTexture->textureIndex]));
                    }
                }

                const auto determineNonCompressedImageFormat = [](int channels) {
                    switch (channels) {
                        case 1:
                            return vk::Format::eR8Unorm;
                        case 2:
                            return vk::Format::eR8G8Unorm;
                        case 4:
                            return vk::Format::eR8G8B8A8Unorm;
                        default:
                            throw std::runtime_error { "Unsupported image channel: channel count must be 1, 2 or 4." };
                    }
                };

                // Copy infos that have to be recorded.
                std::vector<std::tuple<vk::Buffer, vk::Image, std::vector<vk::BufferImageCopy>>> copyInfos;
                copyInfos.reserve(size());

                // Mutex for protecting the insertion racing to stagingBuffers, imageIndicesToGenerateMipmap and copyInfos.
                std::mutex mutex;

                insert_range(threadPool.submit_sequence(std::size_t{ 0 }, usedImageIndices.size(), [&](std::size_t i) {
                    const std::size_t imageIndex = usedImageIndices[i];
                    Info info;

                    // 1. Create images and load data into staging buffers, collect the copy infos.

                    // WARNING: data WOULD BE DESTROYED IN THE FUNCTION (for reducing memory footprint)!
                    // Therefore, I explicitly marked the parameter type of data as stbi_uc*&& (which force the user to
                    // pass it like std::move(data).
                    const auto processNonCompressedImageFromLoadResult = [&](std::uint32_t width, std::uint32_t height, int channels, stbi_uc* &&data) {
                        vku::AllocatedBuffer stagingBuffer {
                            gpu.allocator,
                            vk::BufferCreateInfo {
                                {},
                                width * height * channels,
                                vk::BufferUsageFlagBits::eTransferSrc,
                            },
                            vma::AllocationCreateInfo {
                                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                                vma::MemoryUsage::eAutoPreferHost,
                            },
                        };
                        gpu.allocator.copyMemoryToAllocation(data, stagingBuffer.allocation, 0, stagingBuffer.size);

                        // Now image data copied into stagingBuffer, therefore it should be freed before image
                        // creation to reduce the memory footprint.
                        stbi_image_free(data);

                        vk::Format imageFormat = determineNonCompressedImageFormat(channels);
                        if (srgbImageIndices.contains(imageIndex)) {
                            imageFormat = convertSrgb(imageFormat);
                        }

                        const auto viewFormats = { imageFormat, convertSrgb(imageFormat) };
                        vk::StructureChain createInfo {
                            vk::ImageCreateInfo {
                                {},
                                vk::ImageType::e2D,
                                imageFormat,
                                { width, height, 1 },
                                vku::Image::maxMipLevels(vk::Extent2D { width, height }), 1,
                                vk::SampleCountFlagBits::e1,
                                vk::ImageTiling::eOptimal,
                                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
                            },
                            vk::ImageFormatListCreateInfo { viewFormats },
                        };

                        if (gpu.supportSwapchainMutableFormat == isSrgbFormat(imageFormat)) {
                            createInfo.get().flags |= vk::ImageCreateFlagBits::eMutableFormat;
                        }
                        else {
                            createInfo.unlink<vk::ImageFormatListCreateInfo>();
                        }

                        vku::AllocatedImage image { gpu.allocator, createInfo.get() };

                        std::scoped_lock lock { mutex };
                        imageIndicesToGenerateMipmap.emplace(imageIndex);
                        copyInfos.emplace_back(
                            stagingBuffers.emplace_front(std::move(stagingBuffer)),
                            image,
                            std::vector {
                                vk::BufferImageCopy {
                                    0, 0, 0,
                                    vk::ImageSubresourceLayers { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                                    {}, image.extent,
                                }
                            });

                        return image;
                    };

                    const auto processNonCompressedImageFromMemory = [&](std::span<const stbi_uc> memory) {
                        int width, height, channels;
                        if (!stbi_info_from_memory(memory.data(), memory.size(), &width, &height, &channels)) {
                            throw std::runtime_error { std::format("Failed to get the image info: {}", stbi_failure_reason()) };
                        }

                        if ((channels == 1 && srgbImageIndices.contains(imageIndex) && !gpu.supportR8SrgbImageFormat) ||
                            (channels == 2 && srgbImageIndices.contains(imageIndex) && !gpu.supportR8G8SrgbImageFormat) ||
                            channels == 3) {
                            // Use 4-channel image for best compatibility.
                            info.alphaChannelPadded = true;
                            channels = 4;
                        }

                        stbi_uc* data = stbi_load_from_memory(memory.data(), memory.size(), &width, &height, nullptr, channels);
                        if (!data) {
                            throw std::runtime_error { std::format("Failed to get the image data: {}", stbi_failure_reason()) };
                        }

                        return processNonCompressedImageFromLoadResult(width, height, channels, std::move(data));
                    };

                    const auto processNonCompressedImageFromFile = [&](const char *path) {
                        int width, height, channels;
                        if (!stbi_info(path, &width, &height, &channels)) {
                            throw std::runtime_error { std::format("Failed to get the image info: {}", stbi_failure_reason()) };
                        }

                        if ((channels == 1 && srgbImageIndices.contains(imageIndex) && !gpu.supportR8SrgbImageFormat) ||
                            (channels == 2 && srgbImageIndices.contains(imageIndex) && !gpu.supportR8G8SrgbImageFormat) ||
                            channels == 3) {
                            // Use 4-channel image for best compatibility.
                            info.alphaChannelPadded = true;
                            channels = 4;
                        }

                        stbi_uc* data = stbi_load(path, &width, &height, nullptr, channels);
                        if (!data) {
                            throw std::runtime_error { std::format("Failed to get the image data: {}", stbi_failure_reason()) };
                        }

                        return processNonCompressedImageFromLoadResult(width, height, channels, std::move(data));
                    };

#ifdef SUPPORT_KHR_TEXTURE_BASISU
                    // WARNING: texture WOULD BE DESTROYED IN THE FUNCTION (for reducing memory footprint)!
                    // Therefore, I explicitly marked the parameter type of texture as ktxTexture2*&& (which force the user to
                    // pass it like std::move(texture).
                    const auto processCompressedImageFromLoadResult = [&](ktxTexture2* &&texture) {
                        // Transcode the texture to BC7 format if needed.
                        if (ktxTexture2_NeedsTranscoding(texture)) {
                            // TODO: As glTF specification says, transfer function should be KHR_DF_TRANSFER_SRGB, but
                            //  using it causes error (msg=Feature not included in in-use library or not yet implemented.)
                            //  https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_basisu/README.md#khr_texture_basisu
                            if (KTX_error_code result = ktxTexture2_TranscodeBasis(texture, KTX_TTF_BC7_RGBA, 0); result != KTX_SUCCESS) {
                                throw std::runtime_error { std::format("Failed to transcode the KTX texture: {}", ktxErrorString(result)) };
                            }
                        }

                        vku::AllocatedBuffer stagingBuffer {
                            gpu.allocator,
                            vk::BufferCreateInfo {
                                {},
                                ktxTexture_GetDataSize(ktxTexture(texture)),
                                vk::BufferUsageFlagBits::eTransferSrc,
                            },
                            vma::AllocationCreateInfo {
                                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                                vma::MemoryUsage::eAutoPreferHost,
                            },
                        };
                        gpu.allocator.copyMemoryToAllocation(ktxTexture_GetData(ktxTexture(texture)), stagingBuffer.allocation, 0, stagingBuffer.size);

                        std::vector<vk::BufferImageCopy> copyRegions
                            = ranges::views::upto(texture->numLevels)
                            | std::views::transform([&](std::uint32_t level) {
                                ktx_size_t offset;
                                if (KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, 0, &offset); result != KTX_SUCCESS) {
                                    throw std::runtime_error { std::format("Failed to get the image subresource(level={}) offset: {}", level, ktxErrorString(result)) };
                                }

                                return vk::BufferImageCopy {
                                    offset, 0, 0,
                                    { vk::ImageAspectFlagBits::eColor, level, 0, 1 },
                                    vk::Offset3D{}, vk::Extent3D { vku::Image::mipExtent(vk::Extent2D { texture->baseWidth, texture->baseHeight }, level), 1 },
                                };
                            })
                            | std::ranges::to<std::vector>();

                        vk::Format imageFormat = static_cast<vk::Format>(texture->vkFormat);
                        const auto viewFormats = { imageFormat, convertSrgb(imageFormat) };
                        vk::StructureChain createInfo {
                            vk::ImageCreateInfo {
                                {},
                                vk::ImageType::e2D,
                                imageFormat,
                                { texture->baseWidth, texture->baseHeight, 1 },
                                texture->numLevels, 1,
                                vk::SampleCountFlagBits::e1,
                                vk::ImageTiling::eOptimal,
                                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                            },
                            vk::ImageFormatListCreateInfo { viewFormats },
                        };

                        if (gpu.supportSwapchainMutableFormat == isSrgbFormat(imageFormat)) {
                            createInfo.get().flags |= vk::ImageCreateFlagBits::eMutableFormat;
                        }
                        else {
                            createInfo.unlink<vk::ImageFormatListCreateInfo>();
                        }

                        const bool generateMipmaps = texture->generateMipmaps;
                        if (generateMipmaps) {
                            createInfo.get().usage |= vk::ImageUsageFlagBits::eTransferSrc;
                        }

                        // Now KTX texture data is copied to the staging buffers, and therefore can be destroyed.
                        ktxTexture_Destroy(ktxTexture(texture));

                        vku::AllocatedImage image{ gpu.allocator, createInfo.get() };

                        // Reduce the partial data to the main ones with a lock.
                        std::scoped_lock lock{ mutex };
                        if (generateMipmaps) {
                            imageIndicesToGenerateMipmap.emplace(imageIndex);
                        }
                        copyInfos.emplace_back(
                            stagingBuffers.emplace_front(std::move(stagingBuffer)),
                            image,
                            std::move(copyRegions));

                        return image;
                    };

                    const auto processCompressedImageFromMemory = [&](std::span<const ktx_uint8_t> memory) {
                        ktxTexture2 *texture;
                        if (KTX_error_code result = ktxTexture2_CreateFromMemory(memory.data(), memory.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture); result != KTX_SUCCESS) {
                            throw std::runtime_error { std::format("Failed to get metadata from KTX texture: {}", ktxErrorString(result)) };
                        }

                        return processCompressedImageFromLoadResult(std::move(texture));
                    };

                    const auto processCompressedImageFromFile = [&](const char *path) {
                        ktxTexture2 *texture;
                        if (KTX_error_code result = ktxTexture2_CreateFromNamedFile(path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture); result != KTX_SUCCESS) {
                            throw std::runtime_error { std::format("Failed to get metadata from KTX texture: {}", ktxErrorString(result)) };
                        }

                        return processCompressedImageFromLoadResult(std::move(texture));
                    };
#endif

                    vku::AllocatedImage image = visit(fastgltf::visitor {
                        [&](const fastgltf::sources::Array& array) {
                            switch (array.mimeType) {
                                case fastgltf::MimeType::JPEG: case fastgltf::MimeType::PNG:
                                    return processNonCompressedImageFromMemory(reinterpret_span<const stbi_uc>(std::span { array.bytes }));
#ifdef SUPPORT_KHR_TEXTURE_BASISU
                                case fastgltf::MimeType::KTX2:
                                    return processCompressedImageFromMemory(reinterpret_span<const ktx_uint8_t>(std::span { array.bytes }));
#endif
                                default:
                                    throw gltf::AssetProcessError::IndeterminateImageMimeType;
                            }
                        },
                        [&](const fastgltf::sources::ByteView& byteView) {
                            switch (byteView.mimeType) {
                                case fastgltf::MimeType::JPEG: case fastgltf::MimeType::PNG:
                                    return processNonCompressedImageFromMemory(reinterpret_span<const stbi_uc>(static_cast<std::span<const std::byte>>(byteView.bytes)));
#ifdef SUPPORT_KHR_TEXTURE_BASISU
                                case fastgltf::MimeType::KTX2:
                                    return processCompressedImageFromMemory(reinterpret_span<const ktx_uint8_t>(static_cast<std::span<const std::byte>>(byteView.bytes)));
#endif
                                default:
                                    throw gltf::AssetProcessError::IndeterminateImageMimeType;
                            }
                        },
                        [&](const fastgltf::sources::URI& uri) {
                            if (!uri.uri.isLocalPath()) throw gltf::AssetProcessError::UnsupportedSourceDataType;

                            // As the glTF specification, uri source may doesn't have MIME type. Therefore, we have to determine
                            // the MIME type from the file extension if it isn't provided.
                            const std::filesystem::path extension = uri.uri.fspath().extension();
                            if (ranges::one_of(uri.mimeType, { fastgltf::MimeType::JPEG, fastgltf::MimeType::PNG }) ||
                                ranges::one_of(extension, { ".jpg", ".jpeg", ".png" })) {

                                if (uri.fileByteOffset == 0) {
                                    return processNonCompressedImageFromFile(PATH_C_STR(assetDir / uri.uri.fspath()));
                                }
                                else {
                                    // Non-zero file byte offset is not supported for stbi_load.
                                    std::vector<std::byte> data = loadFileAsBinary(PATH_C_STR(assetDir / uri.uri.fspath()), uri.fileByteOffset);
                                    return processNonCompressedImageFromMemory(reinterpret_span<const stbi_uc>(std::span { data }));
                                }
                            }
#ifdef SUPPORT_KHR_TEXTURE_BASISU
                            else if (uri.mimeType == fastgltf::MimeType::KTX2 || extension == ".ktx2") {
                                if (uri.fileByteOffset == 0) {
                                    return processCompressedImageFromFile(PATH_C_STR(assetDir / uri.uri.fspath()));
                                }
                                else {
                                    // Non-zero file byte offset is not supported for ktxTexture2_CreateFromNamedFile.
                                    std::vector<std::byte> data = loadFileAsBinary(PATH_C_STR(assetDir / uri.uri.fspath()), uri.fileByteOffset);
                                    return processCompressedImageFromMemory(reinterpret_span<const ktx_uint8_t>(std::span { data }));
                                }
                            }
#endif
                            else {
                                throw gltf::AssetProcessError::IndeterminateImageMimeType;
                            }
                        },
                        [&](const fastgltf::sources::BufferView& bufferView) {
                            switch (bufferView.mimeType) {
                                case fastgltf::MimeType::JPEG: case fastgltf::MimeType::PNG:
                                    return processNonCompressedImageFromMemory(reinterpret_span<const stbi_uc>(adapter(asset, bufferView.bufferViewIndex)));
#ifdef SUPPORT_KHR_TEXTURE_BASISU
                                case fastgltf::MimeType::KTX2:
                                    return processCompressedImageFromMemory(reinterpret_span<const ktx_uint8_t>(adapter(asset, bufferView.bufferViewIndex)));
#endif
                                default:
                                    throw gltf::AssetProcessError::IndeterminateImageMimeType;
                            }
                        },
                        // Note: fastgltf::source::Vector should not be handled since it is not used for fastgltf::Image::data.
                        [](const auto&) -> vku::AllocatedImage {
                            throw gltf::AssetProcessError::UnsupportedSourceDataType;
                        },
                    }, asset.images[imageIndex].data);

                    vk::raii::ImageView imageView = createImageView(gpu.device, image);

                    return std::pair { imageIndex, std::tuple { std::move(image), std::move(imageView), info } };
                }).get() | std::views::as_rvalue);

                // 2. Copy image data from staging buffers to images.
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                    {}, {}, {},
                    *this
                        | std::views::values
                        | std::views::elements<0>
                        | std::views::transform([](vk::Image image) {
                            return vk::ImageMemoryBarrier {
                                {}, vk::AccessFlagBits::eTransferWrite,
                                {}, vk::ImageLayout::eTransferDstOptimal,
                                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                                image, vku::fullSubresourceRange(),
                            };
                        })
                        | std::ranges::to<std::vector>());
                for (const auto &[buffer, image, copyRegion] : copyInfos) {
                    cb.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, copyRegion);
                }

                // Release the queue family ownerships of the images (if required).
                if (!empty() && gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
                    cb.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
                        {}, {}, {},
                        *this | std::views::transform(decomposer([&](std::size_t imageIndex, const auto &tuple) {
                            const auto &[image, imageView, info] = tuple;
                            if (imageIndicesToGenerateMipmap.contains(imageIndex)) {
                                // Image data is only inside the mipLevel=0, therefore only queue family ownership
                                // about that portion have to be transferred. New layout should be TRANSFER_SRC_OPTIMAL.
                                return vk::ImageMemoryBarrier {
                                    vk::AccessFlagBits::eTransferWrite, {},
                                    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                                    gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
                                    image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                                };
                            }
                            else {
                                // All subresource range have data, therefore all of their queue family ownership
                                // have to be transferred. New layout should be SHADER_READ_ONLY_OPTIMAL.
                                return vk::ImageMemoryBarrier {
                                    vk::AccessFlagBits::eTransferWrite, {},
                                    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                                    gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
                                    image, vku::fullSubresourceRange(),
                                };
                            }
                        }))
                        | std::ranges::to<std::vector>());
                }
            }, *transferFence);
            std::ignore = gpu.device.waitForFences(*transferFence, true, ~0ULL); // TODO: failure handling

            // TODO: I cannot certain which way is better: 1) use semaphore for submit the transfer and graphics command at once
            //  and clear the staging buffers when all operations are done, or 2) use fences for both command submissions and
            //  destroy the staging buffers earlier. The first way may be better for the performance, but the second way may be
            //  better for the GPU memory footprint. Investigation needed.
            stagingBuffers.clear();

            // Generate image mipmaps using graphics queue.
            const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
            const vk::raii::Fence graphicsFence { gpu.device, vk::FenceCreateInfo{} };
            vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
                // Change image layouts and acquire resource queue family ownerships (optionally).
                cb.pipelineBarrier2KHR({
                    {}, {}, {},
                    vku::unsafeProxy(*this | std::views::transform(decomposer([&](std::size_t imageIndex, const auto &tuple) {
                        const vku::Image &image = get<0>(tuple);
                        // See previous TRANSFER -> GRAPHICS queue family ownership release code to get insight.
                        if (imageIndicesToGenerateMipmap.contains(imageIndex)) {
                            return vk::ImageMemoryBarrier2 {
                                {}, {},
                                vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
                                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                                gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
                                image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                            };
                        }
                        else {
                            return vk::ImageMemoryBarrier2 {
                                {}, {},
                                {}, {},
                                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                                gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
                                image, vku::fullSubresourceRange(),
                            };
                        }
                    }))
                    | std::ranges::to<std::vector>())
                });

                if (imageIndicesToGenerateMipmap.empty()) return;

                recordBatchedMipmapGenerationCommand(
                    cb,
                    imageIndicesToGenerateMipmap
                        | std::views::transform([this](std::size_t imageIndex) -> const vku::Image* {
                            return &get<0>(at(imageIndex));
                        })
                        | std::ranges::to<std::vector>());

                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
                    {}, {}, {},
                    imageIndicesToGenerateMipmap
                        | std::views::transform([this](std::size_t imageIndex) {
                            return vk::ImageMemoryBarrier {
                                vk::AccessFlagBits::eTransferWrite, {},
                                {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                                get<0>(at(imageIndex)), vku::fullSubresourceRange(),
                            };
                        })
                        | std::ranges::to<std::vector>());
            }, *graphicsFence);
            std::ignore = gpu.device.waitForFences(*graphicsFence, true, ~0ULL); // TODO: failure handling
        }

    private:
        [[nodiscard]] static vk::raii::ImageView createImageView(const vk::raii::Device &device, const vku::Image &image);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk::raii::ImageView vk_gltf_viewer::vulkan::image::Images::createImageView(const vk::raii::Device &device, const vku::Image &image) {
    return { device, vk::ImageViewCreateInfo {
        {},
        image,
        vk::ImageViewType::e2D,
        image.format,
        [&]() -> vk::ComponentMapping {
            switch (componentCount(image.format)) {
            case 1:
                // Grayscale: red channel have to be propagated to green/blue channels.
                return { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eOne };
            case 2:
                // Grayscale \w alpha: red channel have to be propagated to green/blue channels, and alpha channel uses given green value.
                return { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG };
            case 4:
                // RGB or RGBA.
                return {};
            }
            std::unreachable();
        }(),
        vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor),
    } };
}