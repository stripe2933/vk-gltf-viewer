module;

#include <cassert>
#include <stb_image.h>

#ifdef USE_KTX
#include <ktx.h>
#endif

export module vkgltf.image;

import std;
export import fastgltf;
export import vku;

export import vkgltf.StagingBufferStorage;

#ifdef _WIN32
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

template <typename T, typename... Ts>
concept one_of = (std::same_as<T, Ts> || ...);

/**
 * @brief Convert sRGB format to linear format, or vice versa.
 * @param format Format to convert. Must have the corresponding sRGB toggled format.
 * @return Corresponding sRGB toggled format.
 * @throw std::invalid_argument If the given format does not have the corresponding sRGB toggled format.
 */
export
[[nodiscard]] vk::Format toggleSrgb(vk::Format format);

struct StagingData {
    vk::Extent2D extent;
    vk::Format format;
    std::uint32_t mipLevels;

    std::variant<std::pair<vku::AllocatedBuffer, std::vector<vk::BufferImageCopy>>, std::vector<vk::MemoryToImageCopy>> data;
    void *hostBackedData;

    [[nodiscard]] static StagingData fromJpgPng(const char *path, const std::function<vk::Format(int)> &formatFn, vma::Allocator *allocator);
    [[nodiscard]] static StagingData fromJpgPng(std::span<const std::byte> memory, const std::function<vk::Format(int)> &formatFn, vma::Allocator *allocator);
    [[nodiscard]] static StagingData fromJpgPng(const vk::Extent2D &extent, stbi_uc* &&data, vk::Format format, vma::Allocator *allocator);
#ifdef USE_KTX
    [[nodiscard]] static StagingData fromKtx(const char *path, vma::Allocator *allocator);
    [[nodiscard]] static StagingData fromKtx(std::span<const std::byte> memory, vma::Allocator *allocator);
    [[nodiscard]] static StagingData fromKtx(ktxTexture2* &&texture, vma::Allocator *allocator);
#endif

    void destroyHostBackedData() noexcept;
};

namespace vkgltf {
    export class Image {
    public:
        enum class MipmapPolicy : std::uint8_t {
            No, /// Set mip levels to 1.
            AllocateOnly, /// Set mip levels to the number of levels in the complete mipmap chain based on image extent.
        };

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        class Config {
        public:
            const BufferDataAdapter &adapter;

            /**
             * @brief Allow the Vulkan image view can be created from the image with mutated sRGB-ness.
             *
             * If this is set to <tt>true</tt>, <tt>vk::ImageCreateFlagBits::eMutableFormat</tt> will be added to the
             * image creation flags, and image format list of sRGB and linear formats is added to the <tt>pNext</tt>
             * chain.
             *
             * This can be useful when you need to display the image whose color space is different from the swapchain.
             * For example, if the swapchain color space is sRGB, the asset's metallic-roughness texture (which must be
             * in the linear color space) will be displayed darker than the actual, as gamma correction is applied to
             * the color attachment write, but not in the sampling stage. For such case, you can set this flag and create
             * the image view with the sRGB format, then the sampling stage will apply the inverse-gamma correction and
             * the image will be displayed properly.
             *
             * @note If this is enabled, the format determined by either <tt>uncompressedImageFormatFn</tt> (for
             * uncompressed images) or <tt>ktxTexture2::vkFormat</tt> (for KTX images) must have the corresponding
             * sRGB format.
             */
            bool allowMutateSrgbFormat = false;

            /**
             * @brief Image layout used during the copy destination.
             *
             * Usually this is set to <tt>eTransferDstOptimal</tt>, but you may set it to <tt>eGeneral</tt> for performance
             * benefits on some image layout agnostic Vulkan driver.
             *
             * @note This layout will be applied only for the image subresource range to be copied. For example, if you
             * set <tt>uncompressedImageMipmapPolicy</tt> to <tt>MipmapPolicy::AllocateOnly</tt>, then the only first
             * mip subresource will have this layout, and the rest will be in the undefined layout.
             */
            vk::ImageLayout imageCopyDstLayout = vk::ImageLayout::eTransferDstOptimal;

            /**
             * @brief Function that determines the uncompressed image format and desired channel count for loading,
             * based on the queried channel count before image decoding.
             *
             * Before decoding the image, the channel count is queried from the image data, and this function is called
             * to determine the format to use for the image. Also, the component count of the returned format is used as
             * channel count for the image decoding.
             *
             * For example, if the original PNG image has 3 channels (RGB), and <tt>uncompressedImageFormatFn(3) == vk::Format::eR8G8B8A8Unorm</tt>,
             * image will be decoded to 4 channels (RGBA).
             */
            std::function<vk::Format(int)> uncompressedImageFormatFn = [](int channels) noexcept {
                switch (channels) {
                    case 1:
                        return vk::Format::eR8Unorm;
                    case 2:
                        return vk::Format::eR8G8Unorm;
                    case 3: // 3-channel image is not widely supported in Vulkan.
                    case 4:
                        return vk::Format::eR8G8B8A8Unorm;
                    default:
                        std::unreachable();
                }
            };

            /**
             * @brief Mipmap policy for uncompressed images.
             */
            MipmapPolicy uncompressedImageMipmapPolicy = MipmapPolicy::No;

            /**
             * @brief Image usage flags for uncompressed images.
             *
             * The final usage flags is determined by combining the given flags with
             * - <tt>vk::ImageUsageFlagBits::eTransferDst</tt> if <tt>stagingInfo</tt> is given.
             * - <tt>vk::ImageUsageFlagBits::eHostTransfer</tt> if <tt>stagingInfo</tt> is not given.
             */
            vk::ImageUsageFlags uncompressedImageUsageFlags = vk::ImageUsageFlagBits::eSampled;

            /**
             * @brief Destination image layout for uncompressed images.
             *
             * @note This layout will be applied only for the copied image subresource range. For example, if you set
             * <tt>uncompressedImageMipmapPolicy</tt> to <tt>MipmapPolicy::AllocateOnly</tt>, then the only first mip
             * subresource will have this layout, and the rest will be in the undefined layout.
             */
            vk::ImageLayout uncompressedImageDstLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        #ifdef USE_KTX
            /**
             * @brief Image usage flags for compressed images.
             *
             * The final usage flags is determined by combining the given flags with
             * - <tt>vk::ImageUsageFlagBits::eTransferDst</tt> if <tt>stagingInfo</tt> is given.
             * - <tt>vk::ImageUsageFlagBits::eHostTransfer</tt> if <tt>stagingInfo</tt> is not given.
             */
            vk::ImageUsageFlags compressedImageUsageFlags = vk::ImageUsageFlagBits::eSampled;

            /**
             * @brief Destination image layout for compressed images.
             *
             * @note This layout will be applied only for the copied image subresource range. For example, if you set
             * <tt>uncompressedImageMipmapPolicy</tt> to <tt>MipmapPolicy::AllocateOnly</tt>, then the only first mip
             * subresource will have this layout, and the rest will be in the undefined layout.
             */
            vk::ImageLayout compressedImageDstLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        #endif

            /**
             * @brief Queue family indices that the image can be concurrently accessed.
             *
             * If its size is less than 2, <tt>sharingMode</tt> of the image will be set to <tt>vk::SharingMode::eExclusive</tt>.
             */
            vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};

            /**
             * @brief Allocation create info for the image.
             */
            vma::AllocationCreateInfo allocationCreateInfo = { {}, vma::MemoryUsage::eAutoPreferDevice };

            /**
             * @brief Information about buffer to image staging, or <tt>nullptr</tt> if <tt>VK_EXT_host_image_copy</tt> is used.
             */
            StagingInfo *stagingInfo = nullptr;

        #if __APPLE__
            vk::ExportMetalObjectTypeFlagBitsEXT metalObjectTypeFlagBits{};
        #endif
        };

        vku::AllocatedImage image;

        /**
         * @brief Vulkan image view for \p image with the same format and component mapping defined as below:
         *
         * - If image is greyscale, it is propagated to RGB components and alpha is set to 1.0.
         * - If image is greyscale with alpha, greyscale is propagated to RGB components and alpha is set to the original alpha.
         * - If image is RGB, it is propagated to RGB components and alpha is set to 1.0.
         * - If image is RGBA, identity component mapping is used.
         */
        vk::raii::ImageView view;

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        Image(
            const fastgltf::Asset &asset,
            const fastgltf::Image &image,
            const std::filesystem::path &directory,
            const vk::raii::Device &device,
            vma::Allocator allocator,
            const Config<BufferDataAdapter> &config = {}
        ) : image { createImage(asset, image, directory, device, allocator, config) },
            view { device, this->image.getViewCreateInfo().setComponents(getComponentMapping(componentCount(this->image.format))) } { }

    private:
        template <typename BufferDataAdapter>
        [[nodiscard]] static vku::AllocatedImage createImage(
            const fastgltf::Asset &asset,
            const fastgltf::Image &image,
            const std::filesystem::path &directory,
            const vk::raii::Device &device,
            vma::Allocator allocator,
            const Config<BufferDataAdapter> &config
        ) {
            vma::Allocator* const nullableAllocator = config.stagingInfo ? &allocator : nullptr;
            StagingData stagingData = visit(fastgltf::visitor {
                [&]<one_of<fastgltf::sources::Array, fastgltf::sources::ByteView, fastgltf::sources::BufferView> T>(const T &embedded) {
                    std::span<const std::byte> memory;
                    if constexpr (std::same_as<T, fastgltf::sources::BufferView>) {
                        memory = config.adapter(asset, embedded.bufferViewIndex);
                    }
                    else {
                        memory = embedded.bytes;
                    }
                    switch (embedded.mimeType) {
                        case fastgltf::MimeType::JPEG: case fastgltf::MimeType::PNG:
                            return StagingData::fromJpgPng(memory, config.uncompressedImageFormatFn, nullableAllocator);
                    #ifdef USE_KTX
                        case fastgltf::MimeType::KTX2:
                            return StagingData::fromKtx(memory, nullableAllocator);
                    #endif
                        default:
                            throw std::runtime_error { "Unknown image MIME type" };
                    }
                },
                [&](const fastgltf::sources::URI &uri) {
                    if (!uri.uri.isLocalPath()) {
                        throw std::runtime_error { "Non-local image URI is not supported." };
                    }

                    if (uri.fileByteOffset != 0) {
                        throw std::runtime_error { "Non-zero file byte offset is not supported for image source." };
                    }

                    const std::filesystem::path filePath = directory / uri.uri.fspath();

                    // Image MIME type definition is guaranteed to be presented only for buffer view in glTF 2.0
                    // specification. Usually external URI sources do not have defined MIME type.
                    // In this case, MIME type determination can be attempted by the file extension inspection.
                    fastgltf::MimeType mimeType = uri.mimeType;
                    if (mimeType == fastgltf::MimeType::None) {
                        const std::filesystem::path extension = uri.uri.fspath().extension();
                        if (extension == ".jpg" || extension == ".jpeg") {
                            mimeType = fastgltf::MimeType::JPEG;
                        }
                        else if (extension == ".png") {
                            mimeType = fastgltf::MimeType::PNG;
                        }
                    #ifdef USE_KTX
                        else if (extension == ".ktx2") {
                            mimeType = fastgltf::MimeType::KTX2;
                        }
                    #endif
                    }

                    switch (mimeType) {
                        case fastgltf::MimeType::JPEG: case fastgltf::MimeType::PNG:
                            return StagingData::fromJpgPng(PATH_C_STR(filePath), config.uncompressedImageFormatFn, nullableAllocator);
                    #ifdef USE_KTX
                        case fastgltf::MimeType::KTX2:
                            return StagingData::fromKtx(PATH_C_STR(filePath), nullableAllocator);
                    #endif
                        default:
                            throw std::runtime_error { "Unknown image MIME type" };
                    }
                },
                [](const auto&) -> StagingData {
                    throw std::runtime_error { "Unsupported image source type." };
                },
            }, image.data);

            std::array<vk::Format, 2> formatList { stagingData.format, {} };
            vk::StructureChain createInfo {
                vk::ImageCreateInfo {
                    {},
                    vk::ImageType::e2D,
                    stagingData.format,
                    vk::Extent3D { stagingData.extent, 1 },
                    stagingData.mipLevels, 1,
                    vk::SampleCountFlagBits::e1,
                    vk::ImageTiling::eOptimal,
                    {},
                    config.queueFamilies.size() < 2 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
                    config.queueFamilies,
                },
                vk::ImageFormatListCreateInfo { formatList },
            #if __APPLE__
                vk::ExportMetalObjectCreateInfoEXT { config.metalObjectTypeFlagBits },
            #endif
            };

        #ifdef USE_KTX
            if (isCompressed(createInfo.get().format)) {
                createInfo.get().usage = config.compressedImageUsageFlags
                    | (config.stagingInfo ? vk::ImageUsageFlagBits::eTransferDst : vk::ImageUsageFlagBits::eHostTransfer);
            }
            else
        #endif
            {
                if (config.uncompressedImageMipmapPolicy != MipmapPolicy::No) {
                    createInfo.get().mipLevels = vku::Image::maxMipLevels(stagingData.extent);
                }

                createInfo.get().usage = config.uncompressedImageUsageFlags
                    | (config.stagingInfo ? vk::ImageUsageFlagBits::eTransferDst : vk::ImageUsageFlagBits::eHostTransfer);
            }

            if (config.allowMutateSrgbFormat) {
                createInfo.get().flags |= vk::ImageCreateFlagBits::eMutableFormat;
                get<1>(formatList) = toggleSrgb(stagingData.format);
            }
            else {
                createInfo.unlink<vk::ImageFormatListCreateInfo>();
            }

        #if __APPLE__
            if (config.metalObjectTypeFlagBits == vk::ExportMetalObjectTypeFlagBitsEXT{}) {
                createInfo.unlink<vk::ExportMetalObjectCreateInfoEXT>();
            }
        #endif

            vku::AllocatedImage result { allocator, createInfo.get(), config.allocationCreateInfo };

            vk::ImageLayout dstLayout;
        #ifdef USE_KTX
            if (isCompressed(stagingData.format)) {
                dstLayout = config.compressedImageDstLayout;
            }
            else
        #endif
            {
                dstLayout = config.uncompressedImageDstLayout;
            }

            const vk::ImageSubresourceRange copiedImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, stagingData.mipLevels, 0, 1 };
            if (config.stagingInfo) {
                // Acquire mutex lock to prevent recording the command buffer from multiple threads.
                std::unique_lock<std::mutex> lock;
                if (config.stagingInfo->mutex) {
                    lock = std::unique_lock { *config.stagingInfo->mutex };
                }

                // Change image layout for copy destination.
                config.stagingInfo->stagingBufferStorage.get().memoryBarrierFromTop(
                    result, config.imageCopyDstLayout,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    copiedImageSubresourceRange);

                // Copy from the buffer to the image with the given regions.
                auto &[buffer, copyRegions] = *get_if<0>(&stagingData.data);
                config.stagingInfo->stagingBufferStorage.get().stage(
                    std::move(buffer), result, config.imageCopyDstLayout, copyRegions);

                // If src/dst image layouts are same and no queue family ownership transfer is needed, memory barrier
                // can be skipped.
                if (config.imageCopyDstLayout != dstLayout || config.stagingInfo->queueFamilyOwnershipTransfer) {
                    const auto &[src, dst] = config.stagingInfo->queueFamilyOwnershipTransfer
                        .value_or(std::pair { vk::QueueFamilyIgnored, vk::QueueFamilyIgnored });
                    config.stagingInfo->stagingBufferStorage.get().memoryBarrierToBottom(
                        result, config.imageCopyDstLayout, dstLayout, src, dst, copiedImageSubresourceRange);
                }
            }
            else {
                // VK_EXT_host_image_copy is used.

                // Change image layout for copy destination.
                vk::HostImageLayoutTransitionInfo layoutTransitionInfo {
                    result, {}, config.imageCopyDstLayout, copiedImageSubresourceRange };
                device.transitionImageLayoutEXT(layoutTransitionInfo);

                // Copy from the host memory to the image.
                device.copyMemoryToImageEXT({ {}, result, config.imageCopyDstLayout, *get_if<1>(&stagingData.data) });
                stagingData.destroyHostBackedData();

                if (config.imageCopyDstLayout != dstLayout) {
                    // Change image layout to the final layout.
                    layoutTransitionInfo.oldLayout = config.imageCopyDstLayout;
                    layoutTransitionInfo.newLayout = dstLayout;
                    device.transitionImageLayoutEXT(layoutTransitionInfo);
                }
            }

            return result;
        }

        [[nodiscard]] static vk::ComponentMapping getComponentMapping(std::uint8_t componentCount) noexcept;
    };

    export template <>
    class Image::Config<fastgltf::DefaultBufferDataAdapter> {
        static constexpr fastgltf::DefaultBufferDataAdapter adapter;

        // Make adapter accessible by Image.
        friend class Image;

    public:
        bool allowMutateSrgbFormat = false;
        vk::ImageLayout imageCopyDstLayout = vk::ImageLayout::eTransferDstOptimal;
        std::function<vk::Format(int)> uncompressedImageFormatFn = [](int channels) noexcept {
            switch (channels) {
                case 1:
                    return vk::Format::eR8Unorm;
                case 2:
                    return vk::Format::eR8G8Unorm;
                case 3:
                    // 3-channel image is not widely supported in Vulkan.
                case 4:
                    return vk::Format::eR8G8B8A8Unorm;
                default:
                    std::unreachable();
            }
        };
        MipmapPolicy uncompressedImageMipmapPolicy = MipmapPolicy::No;
        vk::ImageUsageFlags uncompressedImageUsageFlags = vk::ImageUsageFlagBits::eSampled;
        vk::ImageLayout uncompressedImageDstLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    #ifdef USE_KTX
        vk::ImageUsageFlags compressedImageUsageFlags = vk::ImageUsageFlagBits::eSampled;
        vk::ImageLayout compressedImageDstLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    #endif
        vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};
        vma::AllocationCreateInfo allocationCreateInfo = { {}, vma::MemoryUsage::eAutoPreferDevice };
        StagingInfo *stagingInfo = nullptr;
    #if __APPLE__
        vk::ExportMetalObjectTypeFlagBitsEXT metalObjectTypeFlagBits{};
    #endif
    };

}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk::Format toggleSrgb(vk::Format format) {
    switch (format) {
        #define BIMAP(x, y) \
            case vk::Format::x: return vk::Format::y; \
            case vk::Format::y: return vk::Format::x
        BIMAP(eR8Unorm, eR8Srgb);
        BIMAP(eR8G8Unorm, eR8G8Srgb);
        BIMAP(eR8G8B8Unorm, eR8G8B8Srgb);
        BIMAP(eB8G8R8Unorm, eB8G8R8Srgb);
        BIMAP(eR8G8B8A8Unorm, eR8G8B8A8Srgb);
        BIMAP(eB8G8R8A8Unorm, eB8G8R8A8Srgb);
        BIMAP(eA8B8G8R8UnormPack32, eA8B8G8R8SrgbPack32);
        BIMAP(eBc1RgbUnormBlock, eBc1RgbSrgbBlock);
        BIMAP(eBc1RgbaUnormBlock, eBc1RgbaSrgbBlock);
        BIMAP(eBc2UnormBlock, eBc2SrgbBlock);
        BIMAP(eBc3UnormBlock, eBc3SrgbBlock);
        BIMAP(eBc7UnormBlock, eBc7SrgbBlock);
        BIMAP(eEtc2R8G8B8UnormBlock, eEtc2R8G8B8SrgbBlock);
        BIMAP(eEtc2R8G8B8A1UnormBlock, eEtc2R8G8B8A1SrgbBlock);
        BIMAP(eEtc2R8G8B8A8UnormBlock, eEtc2R8G8B8A8SrgbBlock);
        BIMAP(eAstc4x4UnormBlock, eAstc4x4SrgbBlock);
        BIMAP(eAstc5x4UnormBlock, eAstc5x4SrgbBlock);
        BIMAP(eAstc5x5UnormBlock, eAstc5x5SrgbBlock);
        BIMAP(eAstc6x5UnormBlock, eAstc6x5SrgbBlock);
        BIMAP(eAstc6x6UnormBlock, eAstc6x6SrgbBlock);
        BIMAP(eAstc8x5UnormBlock, eAstc8x5SrgbBlock);
        BIMAP(eAstc8x6UnormBlock, eAstc8x6SrgbBlock);
        BIMAP(eAstc8x8UnormBlock, eAstc8x8SrgbBlock);
        BIMAP(eAstc10x5UnormBlock, eAstc10x5SrgbBlock);
        BIMAP(eAstc10x6UnormBlock, eAstc10x6SrgbBlock);
        BIMAP(eAstc10x8UnormBlock, eAstc10x8SrgbBlock);
        BIMAP(eAstc10x10UnormBlock, eAstc10x10SrgbBlock);
        BIMAP(eAstc12x10UnormBlock, eAstc12x10SrgbBlock);
        BIMAP(eAstc12x12UnormBlock, eAstc12x12SrgbBlock);
        BIMAP(ePvrtc12BppUnormBlockIMG, ePvrtc12BppSrgbBlockIMG);
        BIMAP(ePvrtc14BppUnormBlockIMG, ePvrtc14BppSrgbBlockIMG);
        BIMAP(ePvrtc22BppUnormBlockIMG, ePvrtc22BppSrgbBlockIMG);
        BIMAP(ePvrtc24BppUnormBlockIMG, ePvrtc24BppSrgbBlockIMG);
        #undef BIMAP
        default:
            throw std::invalid_argument { "No corresponding conversion format" };
    }
}

StagingData StagingData::fromJpgPng(
    const char *path,
    const std::function<vk::Format(int)> &formatFn,
    vma::Allocator *allocator
) {
    int width, height, channels;
    if (!stbi_info(path, &width, &height, &channels)) {
        throw std::runtime_error { stbi_failure_reason() };
    }

    const vk::Format format = std::invoke(formatFn, channels);
    channels = componentCount(format);

    stbi_uc *data = stbi_load(path, &width, &height, nullptr, channels);
    if (!data) {
        throw std::runtime_error { stbi_failure_reason() };
    }

    return fromJpgPng({ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) }, std::move(data), format, allocator);
}

StagingData StagingData::fromJpgPng(
    std::span<const std::byte> memory,
    const std::function<vk::Format(int)> &formatFn,
    vma::Allocator *allocator
) {
    int width, height, channels;
    if (!stbi_info_from_memory(reinterpret_cast<stbi_uc const *>(memory.data()), memory.size_bytes(), &width, &height, &channels)) {
        throw std::runtime_error { stbi_failure_reason() };
    }

    const vk::Format format = std::invoke(formatFn, channels);
    channels = componentCount(format);

    stbi_uc *data = stbi_load_from_memory(
        reinterpret_cast<stbi_uc const *>(memory.data()), memory.size_bytes(),
        &width, &height, nullptr, channels);
    if (!data) {
        throw std::runtime_error { stbi_failure_reason() };
    }

    return fromJpgPng({ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) }, std::move(data), format, allocator);
}

StagingData StagingData::fromJpgPng(const vk::Extent2D &extent, stbi_uc *&&data, vk::Format format, vma::Allocator *allocator) {
    StagingData result {
        .extent = extent,
        .format = format,
        .mipLevels = 1,
        .data = [&] {
            if (allocator) {
                std::variant<std::pair<vku::AllocatedBuffer, std::vector<vk::BufferImageCopy>>, std::vector<vk::MemoryToImageCopy>> result {
                    std::in_place_index<0>,
                    std::piecewise_construct,
                    std::forward_as_tuple(
                        *allocator,
                        vk::BufferCreateInfo {
                            {},
                            blockSize(format) * extent.width * extent.height,
                            vk::BufferUsageFlagBits::eTransferSrc,
                        },
                        vma::AllocationCreateInfo {
                            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                            vma::MemoryUsage::eAutoPreferHost,
                        }),
                    std::forward_as_tuple(std::initializer_list<vk::BufferImageCopy> {
                        vk::BufferImageCopy {
                            0, 0, 0,
                            vk::ImageSubresourceLayers { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                            vk::Offset3D {}, vk::Extent3D { extent, 1 },
                        },
                    }),
                };

                allocator->copyMemoryToAllocation(data, get_if<0>(&result)->first.allocation, 0, get_if<0>(&result)->first.size);
                stbi_image_free(data);

                return result;
            }
            else {
                return std::variant<std::pair<vku::AllocatedBuffer, std::vector<vk::BufferImageCopy>>, std::vector<vk::MemoryToImageCopy>> {
                    std::in_place_index<1>,
                    {
                        vk::MemoryToImageCopy {
                            data, 0, 0,
                            vk::ImageSubresourceLayers { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                            vk::Offset3D{}, vk::Extent3D { extent, 1 },
                        },
                    },
                };
            }
        }(),
        .hostBackedData = allocator ? nullptr : data,
    };

    return result;
}

#ifdef USE_KTX
StagingData StagingData::fromKtx(const char *path, vma::Allocator *allocator) {
    ktxTexture2 *texture;
    KTX_error_code result = ktxTexture2_CreateFromNamedFile(path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    if (result != KTX_SUCCESS) {
        throw std::runtime_error { ktxErrorString(result) };
    }

    return fromKtx(std::move(texture), allocator);
}

StagingData StagingData::fromKtx(std::span<const std::byte> memory, vma::Allocator *allocator) {
    ktxTexture2 *texture;
    KTX_error_code result = ktxTexture2_CreateFromMemory(
        reinterpret_cast<const ktx_uint8_t*>(memory.data()), memory.size_bytes(),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    if (result != KTX_SUCCESS) {
        throw std::runtime_error { ktxErrorString(result) };
    }

    return fromKtx(std::move(texture), allocator);
}

StagingData StagingData::fromKtx(ktxTexture2 *&&texture, vma::Allocator *allocator) {
    // Transcode the texture to BC7 format if needed.
    if (ktxTexture2_NeedsTranscoding(texture)) {
        // TODO: As glTF specification says, transfer function should be KHR_DF_TRANSFER_SRGB, but
        //  using it causes error (msg=Feature not included in in-use library or not yet implemented.)
        //  https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_basisu/README.md#khr_texture_basisu
        KTX_error_code result = ktxTexture2_TranscodeBasis(texture, KTX_TTF_BC7_RGBA, 0);
        if (result != KTX_SUCCESS) {
            throw std::runtime_error { std::format("Failed to transcode the KTX texture: {}", ktxErrorString(result)) };
        }
    }

    return {
        .extent = { texture->baseWidth, texture->baseHeight },
        .format = static_cast<vk::Format>(texture->vkFormat),
        .mipLevels = texture->numLevels,
        .data = [&] {
            if (allocator) {
                std::variant<std::pair<vku::AllocatedBuffer, std::vector<vk::BufferImageCopy>>, std::vector<vk::MemoryToImageCopy>> resultVariant {
                    std::in_place_index<0>,
                    std::piecewise_construct,
                    std::forward_as_tuple(
                        *allocator,
                        vk::BufferCreateInfo {
                            {},
                            ktxTexture_GetDataSize(ktxTexture(texture)),
                            vk::BufferUsageFlagBits::eTransferSrc,
                        },
                        vma::AllocationCreateInfo {
                            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                            vma::MemoryUsage::eAutoPreferHost,
                        }),
                    std::forward_as_tuple(
                        std::from_range,
                        std::views::iota(std::uint32_t{}, texture->numLevels)
                            | std::views::transform([&](std::uint32_t level) {
                                ktx_size_t offset;
                                if (KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, 0, &offset); result != KTX_SUCCESS) {
                                    throw std::runtime_error { ktxErrorString(result) };
                                }

                                return vk::BufferImageCopy {
                                    offset, 0, 0,
                                    vk::ImageSubresourceLayers { vk::ImageAspectFlagBits::eColor, level, 0, 1 },
                                    vk::Offset3D{}, vk::Extent3D { vku::Image::mipExtent(vk::Extent2D { texture->baseWidth, texture->baseHeight }, level), 1 },
                                };
                            })),
                };

                // Copy texture data to the staging buffer.
                allocator->copyMemoryToAllocation(
                    ktxTexture_GetData(ktxTexture(texture)), get_if<0>(&resultVariant)->first.allocation,
                    0, get_if<0>(&resultVariant)->first.size);

                // As data is copied, the texture can be destroyed.
                ktxTexture_Destroy(ktxTexture(texture));

                return resultVariant;
            }
            else {
                return std::variant<std::pair<vku::AllocatedBuffer, std::vector<vk::BufferImageCopy>>, std::vector<vk::MemoryToImageCopy>> {
                    std::in_place_index<1>,
                    std::from_range,
                    std::views::iota(std::uint32_t{}, texture->numLevels)
                        | std::views::transform([&, data = ktxTexture_GetData(ktxTexture(texture))](std::uint32_t level) {
                            ktx_size_t offset;
                            if (KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, 0, &offset); result != KTX_SUCCESS) {
                                throw std::runtime_error { ktxErrorString(result) };
                            }

                            return vk::MemoryToImageCopy {
                                data + offset, 0, 0,
                                vk::ImageSubresourceLayers { vk::ImageAspectFlagBits::eColor, level, 0, 1 },
                                vk::Offset3D{}, vk::Extent3D { vku::Image::mipExtent(vk::Extent2D { texture->baseWidth, texture->baseHeight }, level), 1 },
                            };
                        }),
                };
            }
        }(),
        .hostBackedData = allocator ? nullptr : texture,
    };
}
#endif

void StagingData::destroyHostBackedData() noexcept {
    assert(hostBackedData && "Data is not host backed");

#ifdef USE_KTX
    if (isCompressed(format)) {
        ktxTexture_Destroy(ktxTexture(hostBackedData));
    }
    else
#endif
    {
        stbi_image_free(hostBackedData);
    }
}

vk::ComponentMapping vkgltf::Image::getComponentMapping(std::uint8_t componentCount) noexcept {
    switch (componentCount) {
        case 1:
            // Grayscale: red channel have to be propagated to green/blue channels.
            return { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eOne };
        case 2:
            // Grayscale with alpha: red channel have to be propagated to green/blue channels, and alpha channel uses given green value.
            return { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG };
        case 3:
            // RGB.
            return { vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eOne };
        case 4:
            // RGBA.
            return { vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity };
        default:
            std::unreachable();
    }
}