module;

#include <fastgltf/types.hpp>
#include <stb_image.h>
#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:gltf.AssetTextures;

import std;
import ranges;
export import thread_pool;
export import :gltf.AssetExternalBuffers;
import :io.StbDecoder;
export import :vulkan.Gpu;
import :vulkan.mipmap;

/**
 * Convert the span of \p U to the span of \p T. The result span byte size must be same as the \p span's.
 * @tparam T Result span type.
 * @tparam U Source span type.
 * @param span Source span.
 * @return Converted span.
 * @note Since the source and result span sizes must be same, <tt>span.size_bytes()</tt> must be divisible by <tt>sizeof(T)</tt>.
 */
template <typename T, typename U>
[[nodiscard]] auto as_span(std::span<U> span) -> std::span<T> {
    assert(span.size_bytes() % sizeof(T) == 0 && "Span size mismatch: span of T does not fully fit into the current span.");
    return { reinterpret_cast<T*>(span.data()), span.size_bytes() / sizeof(T) };
}

constexpr auto convertSamplerAddressMode(fastgltf::Wrap wrap) noexcept -> vk::SamplerAddressMode {
    switch (wrap) {
        case fastgltf::Wrap::ClampToEdge:
            return vk::SamplerAddressMode::eClampToEdge;
        case fastgltf::Wrap::MirroredRepeat:
            return vk::SamplerAddressMode::eMirroredRepeat;
        case fastgltf::Wrap::Repeat:
            return vk::SamplerAddressMode::eRepeat;
    }
    std::unreachable();
}

namespace vk_gltf_viewer::gltf {
    export class AssetTextures {
        const fastgltf::Asset &asset;
        const vulkan::Gpu &gpu;

        /**
         * Staging buffers for temporary data transfer. This have to be cleared after the transfer command execution
         * finished.
         */
        std::forward_list<vku::AllocatedBuffer> stagingBuffers;

    public:
        /**
         * Asset images. <tt>images[i]</tt> represents <tt>asset.images[i]</tt>.
         * @note Only images that are used by a texture is created.
         */
        std::unordered_map<std::size_t, vku::AllocatedImage> images;

        /**
         * Asset samplers. <tt>samplers[i]</tt> represents <tt>asset.samplers[i]</tt>.
         */
        std::vector<vk::raii::Sampler> samplers = createSamplers();

        AssetTextures(
            const fastgltf::Asset &asset,
            const std::filesystem::path &assetDir,
            const AssetExternalBuffers &externalBuffers,
            const vulkan::Gpu &gpu,
            BS::thread_pool threadPool = {}
        ) : asset { asset },
            gpu { gpu },
            images { createImages(assetDir, externalBuffers, threadPool) } {
            // Transfer the asset resources into the GPU using transfer queue.
            const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
            const vk::raii::Fence transferFence { gpu.device, vk::FenceCreateInfo{} };
            vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
                stageImages(assetDir, externalBuffers, cb, threadPool);

                // Release the queue family ownerships of the images (if required).
                if (!images.empty() && gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
                    cb.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
                        {}, {}, {},
                        images
                            | std::views::values
                            | std::views::transform([&](vk::Image image) {
                                return vk::ImageMemoryBarrier {
                                    vk::AccessFlagBits::eTransferWrite, {},
                                    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                                    gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
                                    image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                                };
                            })
                            | std::ranges::to<std::vector>());
                }
            }, *transferFence);
            if (vk::Result result = gpu.device.waitForFences(*transferFence, true, ~0ULL); result != vk::Result::eSuccess) {
                throw std::runtime_error { std::format("Failed to transfer the asset resources into the GPU: {}", to_string(result)) };
            }

            // TODO: I cannot certain which way is better: 1) use semaphore for submit the transfer and graphics command at once
            //  and clear the staging buffers when all operations are done, or 2) use fences for both command submissions and
            //  destroy the staging buffers earlier. The first way may be better for the performance, but the second way may be
            //  better for the GPU memory footprint. Investigation needed.
            stagingBuffers.clear();

            // Generate image mipmaps using graphics queue.
            const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
            const vk::raii::Fence graphicsFence { gpu.device, vk::FenceCreateInfo{} };
            vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
                if (images.empty()) return;

                // Change image layouts and acquire resource queue family ownerships (optionally).
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                    {}, {}, {},
                    images
                        | std::views::values
                        | std::views::transform([&](vk::Image image) {
                            return vk::ImageMemoryBarrier {
                                {}, vk::AccessFlagBits::eTransferRead,
                                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                                gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
                                image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                            };
                        })
                        | std::ranges::to<std::vector>());

                vulkan::recordBatchedMipmapGenerationCommand(cb, images | std::views::values);

                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
                    {}, {}, {},
                    images
                        | std::views::values
                        | std::views::transform([](vk::Image image) {
                            return vk::ImageMemoryBarrier {
                                vk::AccessFlagBits::eTransferWrite, {},
                                {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                                image, vku::fullSubresourceRange(),
                            };
                        })
                        | std::ranges::to<std::vector>());
            }, *graphicsFence);
            if (vk::Result result = gpu.device.waitForFences(*graphicsFence, true, ~0ULL); result != vk::Result::eSuccess) {
                throw std::runtime_error { std::format("Failed to generate the texture mipmaps: {}", to_string(result)) };
            }
        }

    private:
        [[nodiscard]] auto createImages(
            const std::filesystem::path &assetDir,
            const AssetExternalBuffers &externalBuffers,
            BS::thread_pool &threadPool
        ) const -> std::unordered_map<std::size_t, vku::AllocatedImage> {
            // Base color and emissive texture must be in SRGB format.
            // Therefore, first traverse the asset and fetch the image index that must be in R8G8B8A8Srgb.
            std::unordered_set<std::size_t> srgbImageIndices;
            for (const fastgltf::Material &material : asset.materials) {
                if (const auto &baseColorTexture = material.pbrData.baseColorTexture) {
                    srgbImageIndices.emplace(*asset.textures[baseColorTexture->textureIndex].imageIndex);
                }
                if (const auto &emissiveTexture = material.emissiveTexture) {
                    srgbImageIndices.emplace(*asset.textures[emissiveTexture->textureIndex].imageIndex);
                }
            }

            return threadPool.submit_sequence(std::size_t{ 0 }, asset.textures.size(), [&](std::size_t textureIndex) {
                const std::size_t imageIndex = *asset.textures[textureIndex].imageIndex;

                int width, height, channels;
                visit(fastgltf::visitor {
                    [&](const fastgltf::sources::Array& array) {
                        if (array.mimeType != fastgltf::MimeType::JPEG && array.mimeType != fastgltf::MimeType::PNG) {
                            throw std::runtime_error { "Unsupported image MIME type" };
                        }

                        if (!stbi_info_from_memory(array.bytes.data(), array.bytes.size(), &width, &height, &channels)) {
                            throw std::runtime_error { std::format("Failed to get the image info: {}", stbi_failure_reason()) };
                        }
                    },
                    [&](const fastgltf::sources::URI& uri) {
                        // Check MIME type validity.
                        [&]() {
                            if (uri.mimeType != fastgltf::MimeType::JPEG && uri.mimeType != fastgltf::MimeType::PNG) {
                                // As the glTF specification, uri source may doesn't have MIME type. In this case, we can determine
                                // the MIME type from the file extension.
                                if (auto extension = uri.uri.fspath().extension(); extension == ".jpg" || extension == ".jpeg" || extension == ".png") {
                                    return;
                                }
                                throw std::runtime_error { "Unsupported image MIME type" };
                            }
                        }();

                        if (uri.fileByteOffset != 0) {
                            throw std::runtime_error { "Non-zero file byte offset not supported." };
                        }
                        if (!uri.uri.isLocalPath()) throw std::runtime_error { "Non-local source URI not supported." };

                        if (!stbi_info((assetDir / uri.uri.fspath()).string().c_str(), &width, &height, &channels)) {
                            throw std::runtime_error { std::format("Failed to get the image info: {}", stbi_failure_reason()) };
                        }
                    },
                    [&](const fastgltf::sources::BufferView& bufferView) {
                        if (bufferView.mimeType != fastgltf::MimeType::JPEG && bufferView.mimeType != fastgltf::MimeType::PNG) {
                            throw std::runtime_error { "Unsupported image MIME type" };
                        }

                        const std::span imageDataBuffer = as_span<const std::uint8_t>(
                            externalBuffers.getByteRegion(asset.bufferViews[bufferView.bufferViewIndex]));
                        if (!stbi_info_from_memory(imageDataBuffer.data(), imageDataBuffer.size(), &width, &height, &channels)) {
                            throw std::runtime_error { std::format("Failed to get the image info: {}", stbi_failure_reason()) };
                        }
                    },
                    [](const auto&) {
                        throw std::runtime_error { "Unsupported source data type" };
                    },
                }, asset.images[imageIndex].data);

                const vk::Extent2D imageExtent { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
                const vk::Format imageFormat = [&]() {
                    switch (channels) {
                        case 1:
                            return vk::Format::eR8Unorm;
                        case 2:
                            return vk::Format::eR8G8Unorm;
                        case 3: case 4:
                            return srgbImageIndices.contains(imageIndex) ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
                        default:
                            throw std::runtime_error { "Unsupported image channel: channel count must be 1, 2, 3 or 4." };
                    }
                }();

                return std::pair<std::size_t, vku::AllocatedImage> {
                    std::piecewise_construct,
                    std::tuple { imageIndex },
                    std::forward_as_tuple(gpu.allocator, vk::ImageCreateInfo {
                        {},
                        vk::ImageType::e2D,
                        imageFormat,
                        vk::Extent3D { imageExtent, 1 },
                        vku::Image::maxMipLevels(imageExtent) /* mipmap will be generated in the future */, 1,
                        vk::SampleCountFlagBits::e1,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
                    }),
                };
            }).get() | std::views::as_rvalue | std::ranges::to<std::unordered_map>();
        }

        [[nodiscard]] auto createSamplers() const -> std::vector<vk::raii::Sampler> {
            return asset.samplers
                | std::views::transform([this](const fastgltf::Sampler &sampler) {
                    vk::SamplerCreateInfo createInfo {
                        {},
                        {}, {}, {},
                        convertSamplerAddressMode(sampler.wrapS), convertSamplerAddressMode(sampler.wrapT), {},
                        {},
                        true, 16.f,
                        {}, {},
                        {}, vk::LodClampNone,
                    };

                    // TODO: how can map OpenGL filter to Vulkan corresponds?
                    // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSamplerCreateInfo.html
                    const auto applyFilter = [&](bool mag, fastgltf::Filter filter) -> void {
                        switch (filter) {
                        case fastgltf::Filter::Nearest:
                            (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eNearest;
                            break;
                        case fastgltf::Filter::Linear:
                            (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eLinear;
                            break;
                        case fastgltf::Filter::NearestMipMapNearest:
                            (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eNearest;
                            createInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
                            break;
                        case fastgltf::Filter::LinearMipMapNearest:
                            (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eLinear;
                            createInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
                            break;
                        case fastgltf::Filter::NearestMipMapLinear:
                            (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eNearest;
                            createInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
                            break;
                        case fastgltf::Filter::LinearMipMapLinear:
                            (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eLinear;
                            createInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
                            break;
                        }
                    };
                    if (sampler.magFilter) applyFilter(true, *sampler.magFilter);
                    if (sampler.minFilter) applyFilter(false, *sampler.minFilter);

                    // For best performance, all address mode should be the same.
                    // https://developer.arm.com/documentation/101897/0302/Buffers-and-textures/Texture-and-sampler-descriptors
                    if (createInfo.addressModeU == createInfo.addressModeV) {
                        createInfo.addressModeW = createInfo.addressModeU;
                    }

                    return vk::raii::Sampler { gpu.device, createInfo };
                })
                | std::ranges::to<std::vector>();
        }

        auto stageImages(
            const std::filesystem::path &assetDir,
            const AssetExternalBuffers &externalBuffers,
            vk::CommandBuffer copyCommandBuffer,
            BS::thread_pool &threadPool
        ) -> void {
            if (images.empty()) return;

            const std::vector imageDatas = threadPool.submit_sequence(std::size_t { 0 }, asset.textures.size(), [&](std::size_t textureIndex) {
                const std::size_t imageIndex = *asset.textures[textureIndex].imageIndex;

                const int channels = [imageFormat = images.at(imageIndex).format]() {
                    // TODO: currently image can only has format R8Unorm, R8G8Unorm, R8G8B8A8Unorm or R8G8B8A8Srgb (see createImages()),
                    //  but determining the channel counts from format will be quite hard when using GPU compressed texture. We
                    //  need more robust solution for this.
                    switch (imageFormat) {
                        case vk::Format::eR8Unorm:
                            return 1;
                        case vk::Format::eR8G8Unorm:
                            return 2;
                        case vk::Format::eR8G8B8A8Unorm: case vk::Format::eR8G8B8A8Srgb:
                            return 4;
                        default:
                            std::unreachable(); // This line shouldn't be reached! Recheck createImages() function.
                    }
                }();

                return visit(fastgltf::visitor {
                    [&](const fastgltf::sources::Array& array) {
                        return io::StbDecoder<std::uint8_t>::fromMemory(std::span { array.bytes }, channels);
                    },
                    [&](const fastgltf::sources::URI& uri) {
                        return io::StbDecoder<std::uint8_t>::fromFile((assetDir / uri.uri.fspath()).string().c_str(), channels);
                    },
                    [&](const fastgltf::sources::BufferView& bufferView) {
                        const std::span bufferViewBytes = externalBuffers.getByteRegion(asset.bufferViews[bufferView.bufferViewIndex]);
                        return io::StbDecoder<std::uint8_t>::fromMemory(bufferViewBytes, channels);
                    },
                    [](const auto&) -> io::StbDecoder<std::uint8_t>::DecodeResult {
                        std::unreachable(); // This line shouldn't be reached! Recheck createImages() function.
                    },
                }, asset.images[imageIndex].data);
            }).get();

            const auto &[stagingBuffer, copyOffsets]
                = createCombinedStagingBuffer(imageDatas | std::views::transform([](const auto &x) { return as_bytes(x.asSpan()); }));

            // 1. Change image[mipLevel=0] layouts to vk::ImageLayout::eTransferDstOptimal for staging.
            copyCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                {}, {}, {},
                images
                    | std::views::values
                    | std::views::transform([](vk::Image image) {
                        return vk::ImageMemoryBarrier {
                            {}, vk::AccessFlagBits::eTransferWrite,
                            {}, vk::ImageLayout::eTransferDstOptimal,
                            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                            image, vku::fullSubresourceRange(),
                        };
                    })
                    | std::ranges::to<std::vector>());

            // 2. Copy image data from staging buffer to images.
            for (auto [textureIndex, copyOffset] : copyOffsets | ranges::views::enumerate) {
                const vku::Image &image = images.at(*asset.textures[textureIndex].imageIndex);
                copyCommandBuffer.copyBufferToImage(
                    stagingBuffer,
                    image, vk::ImageLayout::eTransferDstOptimal,
                    vk::BufferImageCopy {
                        copyOffset, 0, 0,
                        vk::ImageSubresourceLayers { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                        { 0, 0, 0 },
                        image.extent,
                    });
            }
        }

        /**
         * From given segments (a range of byte data), create a combined staging buffer and return each segments' start offsets.
         *
         * Example: Two segments { 0xAA, 0xBB, 0xCC } and { 0xDD, 0xEE } will be combined to { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE },
         * and their start offsets are { 0, 3 }.
         * @param segments Data segments to be combined.
         * @return Pair of combined staging buffer and each segments' start offsets vector.
         */
        template <std::ranges::random_access_range R>
            requires std::ranges::contiguous_range<std::ranges::range_value_t<R>>
        [[nodiscard]] auto createCombinedStagingBuffer(
            R &&segments
        ) -> std::pair<const vku::AllocatedBuffer&, std::vector<vk::DeviceSize>> {
            if constexpr (std::convertible_to<std::ranges::range_value_t<R>, std::span<const std::byte>>) {
                assert(!segments.empty() && "Empty segments not allowed (Vulkan requires non-zero buffer size)");

                // Calculate each segments' size and their destination offsets.
                const auto segmentSizes = segments | std::views::transform([](const auto &bytes) { return bytes.size(); });
                std::vector<vk::DeviceSize> copyOffsets(segmentSizes.size());
                std::exclusive_scan(segmentSizes.begin(), segmentSizes.end(), copyOffsets.begin(), vk::DeviceSize { 0 });

                vku::MappedBuffer stagingBuffer { gpu.allocator, vk::BufferCreateInfo {
                    {},
                    copyOffsets.back() + segmentSizes.back(), // = sum(segmentSizes).
                    vk::BufferUsageFlagBits::eTransferSrc,
                } };
                for (auto [segment, copyOffset] : std::views::zip(segments, copyOffsets)){
                    std::ranges::copy(segment, static_cast<std::byte*>(stagingBuffer.data) + copyOffset);
                }

                return { stagingBuffers.emplace_front(std::move(stagingBuffer).unmap()), std::move(copyOffsets) };
            }
            else {
                // Retry with converting each segments into the std::span<const std::byte>.
                const auto byteSegments = segments | std::views::transform([](const auto &segment) { return as_bytes(std::span { segment }); });
                return createCombinedStagingBuffer(byteSegments);
            }
        }
    };
}