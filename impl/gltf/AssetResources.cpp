module;

#include <version>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <mikktspace.h>
#include <stb_image.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetResources;

import std;
import ranges;
import thread_pool;
import :gltf.algorithm.MikktSpaceInterface;
import :io.StbDecoder;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

template <typename T>
[[nodiscard]] auto to_optional(fastgltf::OptionalWithFlagValue<T> v) noexcept -> std::optional<T> {
    if (v) return *v;
    return std::nullopt;
}

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

/**
 * Create new Vulkan buffer that has the same size as \p srcBuffer and usage with \p dstBufferUsage and
 * <tt>vk::BufferUsageFlagBits::eTransferDst</tt>, then record the copy commands from \p srcBuffer to the new buffer
 * into \p copyCommandBuffer.
 * @param allocator VMA allocator that used for buffer allocation.
 * @param srcBuffer Source buffer that will be copied.
 * @param dstBufferUsage Usage flags for the new buffer. <tt>vk::BufferUsageFlagBits::eTransferDst</tt> will be added.
 * @param queueFamilyIndices Queue family indices that the new buffer will be shared. If you want to use the explicit
 * queue family ownership management, you can pass the empty span.
 * @param copyCommandBuffer Command buffer that will record the copy commands.
 * @return Allocated buffer that has the same size as \p srcBuffer. After \p copyCommandBuffer execution finished, the
 * data in \p srcBuffer will be copied to the returned buffer.
 */
[[nodiscard]] auto createStagingDstBuffer(
    vma::Allocator allocator,
    const vku::Buffer &srcBuffer,
    vk::BufferUsageFlags dstBufferUsage,
    std::span<const std::uint32_t> queueFamilyIndices,
    vk::CommandBuffer copyCommandBuffer
) -> vku::AllocatedBuffer {
    vku::AllocatedBuffer dstBuffer { allocator, vk::BufferCreateInfo {
        {},
        srcBuffer.size,
        dstBufferUsage | vk::BufferUsageFlagBits::eTransferDst,
        (queueFamilyIndices.size() < 2) ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
        queueFamilyIndices,
    } };
    copyCommandBuffer.copyBuffer(
        srcBuffer, dstBuffer,
        vk::BufferCopy { 0, 0, srcBuffer.size });
    return dstBuffer;
}

/**
 * For combined staging buffer \p srcBuffer, create Vulkan buffers that have the same sizes of each staging segments and
 * usages with \p dstBufferUsages and <tt>vk::BufferUsageFlagBits::eTransferDst</tt>, then record the copy commands from
 * \p srcBuffer to the new buffers into \p copyCommandBuffer.
 * @param allocator VMA allocator that used for buffer allocation.
 * @param srcBuffer Combined staging buffer that contains multiple segments.
 * @param copyInfos Copy information that contains each segment's start offset, copy size and destination buffer usage.
 * @param queueFamilyIndices Queue family indices that the new buffers will be shared. If you want to use the explicit
 * queue family ownership management, you can pass the empty span.
 * @param copyCommandBuffer Command buffer that will record the copy commands.
 * @return Allocated buffers that have the same sizes of each staging segments. After \p copyCommandBuffer execution
 * finished, the data in \p srcBuffer will be copied to the returned buffers.
 * @note The returned buffers will be ordered by the \p copyInfos.
 * @note Each \p copyInfos' copying size must be nonzero.
 */
[[nodiscard]] auto createStagingDstBuffers(
    vma::Allocator allocator,
    vk::Buffer srcBuffer,
    std::ranges::random_access_range auto &&copyInfos,
    std::span<const std::uint32_t> queueFamilyIndices,
    vk::CommandBuffer copyCommandBuffer
) -> std::vector<vku::AllocatedBuffer> {
    const vk::SharingMode sharingMode = (queueFamilyIndices.size() < 2)
        ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;
    return copyInfos
        | ranges::views::decompose_transform([&](vk::DeviceSize srcOffset, vk::DeviceSize copySize, vk::BufferUsageFlags dstBufferUsage) {
            vku::AllocatedBuffer dstBuffer { allocator, vk::BufferCreateInfo {
                {},
                copySize,
                dstBufferUsage | vk::BufferUsageFlagBits::eTransferDst,
                sharingMode,
                queueFamilyIndices,
            } };
            copyCommandBuffer.copyBuffer(
                srcBuffer, dstBuffer,
                vk::BufferCopy { srcOffset, 0, copySize });
            return dstBuffer;
        })
        | std::ranges::to<std::vector>();
}

vk_gltf_viewer::gltf::AssetResources::AssetResources(
    const fastgltf::Asset &asset,
    const std::filesystem::path &assetDir,
    const AssetExternalBuffers &externalBuffers,
    const vulkan::Gpu &gpu,
    BS::thread_pool threadPool
) : asset { asset },
    gpu { gpu },
    images { createImages(assetDir, externalBuffers, threadPool) } {
    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
    const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
        stageImages(assetDir, externalBuffers, cb, threadPool);
        stageMaterials(cb);
        stagePrimitiveAttributeBuffers(externalBuffers, cb);
        stagePrimitiveIndexedAttributeMappingBuffers(IndexedAttribute::Texcoord, cb);
        stagePrimitiveIndexedAttributeMappingBuffers(IndexedAttribute::Color, cb);
        stagePrimitiveTangentBuffers(externalBuffers, cb, threadPool);
        stagePrimitiveIndexBuffers(externalBuffers, cb);

        releaseResourceQueueFamilyOwnership(gpu.queueFamilies, cb);
    }, *fence);
    if (vk::Result result = gpu.device.waitForFences(*fence, true, ~0ULL); result != vk::Result::eSuccess) {
        throw std::runtime_error { std::format("Failed to transfer the asset resources into the GPU: {}", to_string(result)) };
    }

    stagingBuffers.clear();
}

auto vk_gltf_viewer::gltf::AssetResources::createPrimitiveInfos() const -> std::unordered_map<const fastgltf::Primitive*, PrimitiveInfo> {
    std::unordered_map<const fastgltf::Primitive*, PrimitiveInfo> primitiveInfos;
    for (const fastgltf::Node &node : asset.nodes){
        if (!node.meshIndex) continue;

        for (const fastgltf::Primitive &primitive : asset.meshes[*node.meshIndex].primitives){
            primitiveInfos.try_emplace(&primitive, to_optional(primitive.materialIndex));
        }
    }

    return primitiveInfos;
}

auto vk_gltf_viewer::gltf::AssetResources::createImages(
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

    return threadPool.submit_sequence(0UZ, asset.textures.size(), [&](std::size_t textureIndex) {
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

                if (!stbi_info((assetDir / uri.uri.fspath()).c_str(), &width, &height, &channels)) {
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

auto vk_gltf_viewer::gltf::AssetResources::createSamplers() const -> std::unordered_map<std::size_t, vk::raii::Sampler> {
    return asset.textures
        | std::views::filter([&](const fastgltf::Texture &texture) { return texture.samplerIndex.has_value(); })
        | std::views::transform([&](const fastgltf::Texture &texture) {
            constexpr auto convertSamplerAddressMode = [](fastgltf::Wrap wrap) noexcept -> vk::SamplerAddressMode {
                switch (wrap) {
                    case fastgltf::Wrap::ClampToEdge:
                        return vk::SamplerAddressMode::eClampToEdge;
                    case fastgltf::Wrap::MirroredRepeat:
                        return vk::SamplerAddressMode::eMirroredRepeat;
                    case fastgltf::Wrap::Repeat:
                        return vk::SamplerAddressMode::eRepeat;
                }
                std::unreachable();
            };

            const std::size_t samplerIndex = *texture.samplerIndex;
            const fastgltf::Sampler &sampler = asset.samplers[samplerIndex];
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

            return std::pair<std::size_t, vk::raii::Sampler> {
                std::piecewise_construct,
                std::tuple { samplerIndex },
                std::forward_as_tuple(gpu.device, createInfo),
            };
        })
        | std::ranges::to<std::unordered_map>();
}

auto vk_gltf_viewer::gltf::AssetResources::createMaterialBuffer() const -> vku::AllocatedBuffer {
    return { gpu.allocator, vk::BufferCreateInfo {
        {},
        sizeof(GpuMaterial) * (1 /* fallback material */ + asset.materials.size()),
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    } };
}

auto vk_gltf_viewer::gltf::AssetResources::stageImages(
    const std::filesystem::path &assetDir,
    const AssetExternalBuffers &externalBuffers,
    vk::CommandBuffer copyCommandBuffer,
    BS::thread_pool &threadPool
) -> void {
    if (images.empty()) return;

    const std::vector imageDatas = threadPool.submit_sequence(0UZ, asset.textures.size(), [&](std::size_t textureIndex) {
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
                return io::StbDecoder<std::uint8_t>::fromFile((assetDir / uri.uri.fspath()).c_str(), channels);
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

auto vk_gltf_viewer::gltf::AssetResources::stageMaterials(vk::CommandBuffer copyCommandBuffer) -> void {
    std::vector<GpuMaterial> materials;
    materials.reserve(asset.materials.size() + 1);
    materials.push_back({}); // Fallback material.
    materials.append_range(asset.materials | std::views::transform([&](const fastgltf::Material &material) {
        GpuMaterial gpuMaterial {
            .baseColorFactor = glm::gtc::make_vec4(material.pbrData.baseColorFactor.data()),
            .metallicFactor = material.pbrData.metallicFactor,
            .roughnessFactor = material.pbrData.roughnessFactor,
            .emissiveFactor = glm::gtc::make_vec3(material.emissiveFactor.data()),
            .alphaCutOff = material.alphaCutoff,
        };

        if (const auto &baseColorTexture = material.pbrData.baseColorTexture) {
            gpuMaterial.baseColorTexcoordIndex = baseColorTexture->texCoordIndex;
            gpuMaterial.baseColorTextureIndex = static_cast<std::int16_t>(baseColorTexture->textureIndex);
        }
        if (const auto &metallicRoughnessTexture = material.pbrData.metallicRoughnessTexture) {
            gpuMaterial.metallicRoughnessTexcoordIndex = metallicRoughnessTexture->texCoordIndex;
            gpuMaterial.metallicRoughnessTextureIndex = static_cast<std::int16_t>(metallicRoughnessTexture->textureIndex);
        }
        if (const auto &normalTexture = material.normalTexture) {
            gpuMaterial.normalTexcoordIndex = normalTexture->texCoordIndex;
            gpuMaterial.normalTextureIndex = static_cast<std::int16_t>(normalTexture->textureIndex);
            gpuMaterial.normalScale = normalTexture->scale;
        }
        if (const auto &occlusionTexture = material.occlusionTexture) {
            gpuMaterial.occlusionTexcoordIndex = occlusionTexture->texCoordIndex;
            gpuMaterial.occlusionTextureIndex = static_cast<std::int16_t>(occlusionTexture->textureIndex);
            gpuMaterial.occlusionStrength = occlusionTexture->strength;
        }
        if (const auto &emissiveTexture = material.emissiveTexture) {
            gpuMaterial.emissiveTexcoordIndex = emissiveTexture->texCoordIndex;
            gpuMaterial.emissiveTextureIndex = static_cast<std::int16_t>(emissiveTexture->textureIndex);
        }

        return gpuMaterial;
    }));

    const vk::Buffer stagingBuffer = stagingBuffers.emplace_front(
        vku::MappedBuffer { gpu.allocator, std::from_range, materials, vk::BufferUsageFlagBits::eTransferSrc }.unmap());
    copyCommandBuffer.copyBuffer(stagingBuffer, materialBuffer, vk::BufferCopy { 0, 0, materialBuffer.size });
}

auto vk_gltf_viewer::gltf::AssetResources::stagePrimitiveAttributeBuffers(
    const AssetExternalBuffers &externalBuffers,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    const auto primitives = asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join;

    // Get buffer view indices that are used in primitive attributes.
    auto attributeAccessorIndices = primitives
        | std::views::transform([](const fastgltf::Primitive &primitive) {
            return primitive.attributes | std::views::values;
        })
        | std::views::join;
    const std::unordered_set attributeBufferViewIndices
        = attributeAccessorIndices
        | std::views::transform([&](std::size_t accessorIndex) {
            const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];

            // Check accessor validity.
            if (accessor.sparse) throw std::runtime_error { "Sparse attribute accessor not supported" };
            if (accessor.normalized) throw std::runtime_error { "Normalized attribute accessor not supported" };

            return *accessor.bufferViewIndex;
        })
        | std::ranges::to<std::unordered_set>();

    // Ordered sequence of (bufferViewIndex, bufferViewBytes) pairs.
    const std::vector attributeBufferViewBytes
        = attributeBufferViewIndices
        | std::views::transform([&](std::size_t bufferViewIndex) {
            return std::pair { bufferViewIndex, externalBuffers.getByteRegion(asset.bufferViews[bufferViewIndex]) };
        })
        | std::ranges::to<std::vector>();

    // Create the combined staging buffer that contains all attributeBufferViewBytes.
    const auto &[stagingBuffer, copyOffsets] = createCombinedStagingBuffer(attributeBufferViewBytes | std::views::values);

    // Create device local buffers for each attributeBufferViewBytes, and record copy commands to the copyCommandBuffer.
    attributeBuffers = createStagingDstBuffers(
        gpu.allocator,
        stagingBuffer,
        ranges::views::zip_transform([](std::span<const std::byte> bufferViewBytes, vk::DeviceSize srcOffset) {
            return std::tuple {
                srcOffset,
                bufferViewBytes.size(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            };
        }, attributeBufferViewBytes | std::views::values, copyOffsets),
        gpu.queueFamilies.getUniqueIndices(),
        copyCommandBuffer);

    // Hashmap that can get buffer device address by corresponding buffer view index.
    const std::unordered_map bufferDeviceAddressMappings
        = std::views::zip(
            attributeBufferViewBytes | std::views::keys,
            attributeBuffers | std::views::transform([&](vk::Buffer buffer) { return gpu.device.getBufferAddress({ buffer }); }))
        | std::ranges::to<std::unordered_map>();

    // Iterate over the primitives and set their attribute infos.
    for (const fastgltf::Primitive &primitive : primitives) {
        PrimitiveInfo &primitiveInfo = primitiveInfos[&primitive];
        for (const auto &[attributeName, accessorIndex] : primitive.attributes) {
            const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
            const auto getAttributeBufferInfo = [&]() -> PrimitiveInfo::AttributeBufferInfo {
                const std::size_t byteStride
                    = asset.bufferViews[*accessor.bufferViewIndex].byteStride
                    .value_or(getElementByteSize(accessor.type, accessor.componentType));
                if (!std::in_range<std::uint8_t>(byteStride)) throw std::runtime_error { "Too large byteStride" };
                return {
                    .address = bufferDeviceAddressMappings.at(*accessor.bufferViewIndex) + accessor.byteOffset,
                    .byteStride = static_cast<std::uint8_t>(byteStride),
                };
            };

            constexpr auto parseIndex = [](std::string_view str) {
                std::size_t index;
                auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), index);
                assert(ec == std::errc{} && "Failed to parse std::size_t");
                return index;
            };

            using namespace std::string_view_literals;

            if (attributeName == "POSITION"sv) {
                primitiveInfo.positionInfo = getAttributeBufferInfo();
                primitiveInfo.drawCount = accessor.count;
            }
            // For std::optional, they must be initialized before being accessed.
            else if (attributeName == "NORMAL"sv) {
                primitiveInfo.normalInfo.emplace(getAttributeBufferInfo());
            }
            else if (attributeName == "TANGENT"sv) {
                primitiveInfo.tangentInfo.emplace(getAttributeBufferInfo());
            }
            // Otherwise, attributeName has form of <TEXCOORD_i> or <COLOR_i>.
            else if (constexpr auto prefix = "TEXCOORD_"sv; attributeName.starts_with(prefix)) {
                const std::size_t texcoordIndex = parseIndex(std::string_view { attributeName }.substr(prefix.size()));
                primitiveInfo.texcoordInfos[texcoordIndex] = getAttributeBufferInfo();
            }
            else if (constexpr auto prefix = "COLOR_"sv; attributeName.starts_with(prefix)) {
                const std::size_t colorIndex = parseIndex(std::string_view { attributeName }.substr(prefix.size()));
                primitiveInfo.colorInfos[colorIndex] = getAttributeBufferInfo();
            }
        }
    }
}

auto vk_gltf_viewer::gltf::AssetResources::stagePrimitiveIndexedAttributeMappingBuffers(
    IndexedAttribute attributeType,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    const std::vector attributeBufferInfos
        = primitiveInfos
        | std::views::values
        | std::views::transform([attributeType](const PrimitiveInfo &primitiveInfo) {
            const auto &targetAttributeInfoMap = [=]() -> decltype(auto) {
                switch (attributeType) {
                    case IndexedAttribute::Texcoord: return primitiveInfo.texcoordInfos;
                    case IndexedAttribute::Color: return primitiveInfo.colorInfos;
                }
                std::unreachable(); // Invalid attributeType: must be Texcoord or Color
            }();

            return std::views::iota(0U, targetAttributeInfoMap.size())
                | std::views::transform([&](std::size_t i) {
                    return ranges::value_or(targetAttributeInfoMap, i, {});
                })
                | std::ranges::to<std::vector>();
        })
        | std::ranges::to<std::vector>();

    // If there's no attributeBufferInfo to process, skip processing.
    for (std::span attributeBufferInfo : attributeBufferInfos) {
        if (!attributeBufferInfo.empty()) {
            goto HAS_ATTRIBUTE_BUFFER;
        }
    }
    return;

HAS_ATTRIBUTE_BUFFER:
    const auto &[stagingBuffer, copyOffsets] = createCombinedStagingBuffer(attributeBufferInfos);
    const vk::Buffer indexAttributeMappingBuffer = indexedAttributeMappingBuffers.try_emplace(
        attributeType,
        createStagingDstBuffer(
            gpu.allocator, stagingBuffer,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            gpu.queueFamilies.getUniqueIndices(),
            copyCommandBuffer)).first /* iterator */->second;

    const vk::DeviceAddress pIndexAttributeMappingBuffer = gpu.device.getBufferAddress({ indexAttributeMappingBuffer });
    for (auto &&[primitiveInfo, copyOffset] : std::views::zip(primitiveInfos | std::views::values, copyOffsets)) {
        primitiveInfo.indexedAttributeMappingInfos.try_emplace(
            attributeType,
            pIndexAttributeMappingBuffer + copyOffset);
    }
}

auto vk_gltf_viewer::gltf::AssetResources::stagePrimitiveTangentBuffers(
    const AssetExternalBuffers &externalBuffers,
    vk::CommandBuffer copyCommandBuffer,
    BS::thread_pool &threadPool
) -> void {
    std::vector missingTangentMeshes
        = primitiveInfos
        | std::views::filter([&](const auto &keyValue) {
            const auto &[pPrimitive, primitiveInfo] = keyValue;

            // Skip if primitive already has a tangent attribute.
            if (primitiveInfo.tangentInfo) return false;
            // Skip if primitive doesn't have a material.
            if (const auto &materialIndex = pPrimitive->materialIndex; !materialIndex) return false;
            // Skip if primitive doesn't have a normal texture.
            else if (!asset.materials[*materialIndex].normalTexture) return false;
            // Skip if primitive is non-indexed geometry (screen-space normal and tangent will be generated in the shader).
            return pPrimitive->indicesAccessor.has_value();
        })
        | std::views::keys
        | std::views::transform([&](const fastgltf::Primitive *pPrimitive) {
            // Validate the constraints for MikktSpaceInterface.
            if (auto normalIt = pPrimitive->findAttribute("NORMAL"); normalIt == pPrimitive->attributes.end()) {
                throw std::runtime_error { "Missing NORMAL attribute" };
            }
            else if (auto texcoordIt = pPrimitive->findAttribute(std::format("TEXCOORD_{}", asset.materials[*pPrimitive->materialIndex].normalTexture->texCoordIndex));
                texcoordIt == pPrimitive->attributes.end()) {
                throw std::runtime_error { "Missing TEXCOORD attribute" };
            }
            else return algorithm::MikktSpaceMesh {
                asset,
                asset.accessors[*pPrimitive->indicesAccessor],
                asset.accessors[pPrimitive->findAttribute("POSITION")->second],
                asset.accessors[normalIt->second],
                asset.accessors[texcoordIt->second],
                externalBuffers,
            };
        })
        | std::ranges::to<std::vector>();
    if (missingTangentMeshes.empty()) return; // Skip if there's no missing tangent mesh.

    threadPool.submit_loop(0UZ, missingTangentMeshes.size(), [&](std::size_t meshIndex) {
        auto& mesh = missingTangentMeshes[meshIndex];

        SMikkTSpaceInterface* const pInterface
            = [indexType = mesh.indicesAccessor.componentType]() -> SMikkTSpaceInterface* {
                switch (indexType) {
                    case fastgltf::ComponentType::UnsignedByte:
                        return &algorithm::mikktSpaceInterface<std::uint8_t, AssetExternalBuffers>;
                    case fastgltf::ComponentType::UnsignedShort:
                        return &algorithm::mikktSpaceInterface<std::uint16_t, AssetExternalBuffers>;
                    case fastgltf::ComponentType::UnsignedInt:
                        return &algorithm::mikktSpaceInterface<std::uint32_t, AssetExternalBuffers>;
                    default:
                        throw std::runtime_error{ "Unsupported index type: only unsigned byte/short/int are supported." };
                }
            }();
        if (const SMikkTSpaceContext context{ pInterface, &mesh }; !genTangSpaceDefault(&context)) {
            throw std::runtime_error{ "Failed to generate the tangent attributes" };
        }
    }).wait();

    const auto &[stagingBuffer, copyOffsets]
        = createCombinedStagingBuffer(missingTangentMeshes | std::views::transform([](const auto &mesh) { return as_bytes(std::span { mesh.tangents }); }));
    tangentBuffer.emplace(
        createStagingDstBuffer(
            gpu.allocator, stagingBuffer,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            gpu.queueFamilies.getUniqueIndices(),
            copyCommandBuffer));
    const vk::DeviceAddress pTangentBuffer = gpu.device.getBufferAddress({ tangentBuffer->buffer });

    for (auto &&[primitiveInfo, copyOffset] : std::views::zip(primitiveInfos | std::views::values, copyOffsets)) {
        primitiveInfo.tangentInfo.emplace(pTangentBuffer + copyOffset, 16);
    }
}

auto vk_gltf_viewer::gltf::AssetResources::stagePrimitiveIndexBuffers(
    const AssetExternalBuffers &externalBuffers,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    // Primitive that are contains an indices accessor.
    auto indexedPrimitives = asset.meshes
        | std::views::transform(&fastgltf::Mesh::primitives)
        | std::views::join
        | std::views::filter([](const fastgltf::Primitive &primitive) { return primitive.indicesAccessor.has_value(); });

    // Index data is either
    // - span of the buffer view region, or
    // - vector of uint16 indices if accessor have unsigned byte component and native uint8 index is not supported.
    std::vector<std::vector<std::uint16_t>> generated16BitIndices;
    std::unordered_map<vk::IndexType, std::vector<std::pair<const fastgltf::Primitive*, std::span<const std::byte>>>> indexBufferBytesByType;

    // Get buffer view bytes from indexedPrimtives and group them by index type.
    for (const fastgltf::Primitive &primitive : indexedPrimitives) {
        const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];

        // Check accessor validity.
        if (accessor.sparse) throw std::runtime_error { "Sparse indices accessor not supported" };
        if (accessor.normalized) throw std::runtime_error { "Normalized indices accessor not supported" };

        // Vulkan does not support interleaved index buffer.
        if (const auto& byteStride = asset.bufferViews[*accessor.bufferViewIndex].byteStride) {
            const std::size_t componentByteSize = getElementByteSize(accessor.type, accessor.componentType);
            if (*byteStride != componentByteSize) {
                throw std::runtime_error { "Interleaved index buffer not supported" };
            }
        }

        if (accessor.componentType == fastgltf::ComponentType::UnsignedByte && !gpu.supportUint8Index) {
            // Make vector of uint16 indices.
            std::vector<std::uint16_t> indices(accessor.count);
            iterateAccessorWithIndex<std::uint8_t>(asset, accessor, [&](std::uint8_t index, std::size_t i) {
                indices[i] = index; // Index converted from uint8 to uint16.
            }, externalBuffers);

            indexBufferBytesByType[vk::IndexType::eUint16].emplace_back(
                &primitive,
                as_bytes(std::span { generated16BitIndices.emplace_back(std::move(indices)) }));
        }
        else {
            const vk::IndexType indexType = [&]() -> vk::IndexType {
                switch (accessor.componentType) {
                    case fastgltf::ComponentType::UnsignedByte: return vk::IndexType::eUint8KHR;
                    case fastgltf::ComponentType::UnsignedShort: return vk::IndexType::eUint16;
                    case fastgltf::ComponentType::UnsignedInt: return vk::IndexType::eUint32;
                    default: throw std::runtime_error { "Unsupported index type: only Uint8KHR, Uint16 and Uint32 are supported." };
                }
            }();
            indexBufferBytesByType[indexType].emplace_back(&primitive, externalBuffers.getByteRegion(accessor));
        }
    }

    // Combine index data into a single staging buffer, and create GPU local buffers for each index data. Record copy
    // commands to copyCommandBuffer.
    indexBuffers = indexBufferBytesByType
        | ranges::views::decompose_transform([&](vk::IndexType indexType, const auto &bufferBytes) {
            const auto &[stagingBuffer, copyOffsets] = createCombinedStagingBuffer(bufferBytes | std::views::values);
            auto indexBuffer = createStagingDstBuffer(gpu.allocator, stagingBuffer, vk::BufferUsageFlagBits::eIndexBuffer, gpu.queueFamilies.getUniqueIndices(), copyCommandBuffer);

            for (auto [pPrimitive, offset] : std::views::zip(bufferBytes | std::views::keys, copyOffsets)) {
                PrimitiveInfo &primitiveInfo = primitiveInfos[pPrimitive];
                primitiveInfo.indexInfo.emplace(offset, indexType);
                primitiveInfo.drawCount = asset.accessors[*pPrimitive->indicesAccessor].count;
            }

            return std::pair { indexType, std::move(indexBuffer) };
        })
        | std::ranges::to<std::unordered_map>();
}

auto vk_gltf_viewer::gltf::AssetResources::releaseResourceQueueFamilyOwnership(
    const vulkan::QueueFamilies &queueFamilies,
    vk::CommandBuffer commandBuffer
) const -> void {
    if (queueFamilies.transfer == queueFamilies.graphicsPresent) return;

    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
        {}, {}, {},
        images
            | std::views::values
            | std::views::transform([&](vk::Image image) {
                return vk::ImageMemoryBarrier {
                    vk::AccessFlagBits::eTransferWrite, {},
                    {}, {},
                    queueFamilies.transfer, queueFamilies.graphicsPresent,
                    image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                };
            })
            | std::ranges::to<std::vector>());
}