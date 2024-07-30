module;

#include <version>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <mikktspace.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetResources;

import std;
import :gltf.algorithm.MikktSpaceInterface;
import :helpers.ranges;
import :io.StbDecoder;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

using namespace std::views;
using namespace std::string_view_literals;

[[nodiscard]] auto createStagingDstBuffer(
    vma::Allocator allocator,
    const vku::Buffer &srcBuffer,
    vk::BufferUsageFlags dstBufferUsage,
    vk::CommandBuffer copyCommandBuffer
) -> vku::AllocatedBuffer {
    vku::AllocatedBuffer dstBuffer { allocator, vk::BufferCreateInfo {
        {},
        srcBuffer.size,
        dstBufferUsage | vk::BufferUsageFlagBits::eTransferDst,
    } };
    copyCommandBuffer.copyBuffer(
        srcBuffer, dstBuffer,
        vk::BufferCopy { 0, 0, srcBuffer.size });
    return dstBuffer;
}

[[nodiscard]] auto createStagingDstBuffers(
    vma::Allocator allocator,
    vk::Buffer srcBuffer,
    std::ranges::random_access_range auto &&copyInfos,
    vk::CommandBuffer copyCommandBuffer
) -> std::vector<vku::AllocatedBuffer> {
    return copyInfos
        | transform([&](const auto &copyInfo) {
            const auto [srcOffset, copySize, dstBufferUsage] = copyInfo;
            vku::AllocatedBuffer dstBuffer { allocator, vk::BufferCreateInfo {
                {},
                copySize,
                dstBufferUsage | vk::BufferUsageFlagBits::eTransferDst,
            } };
            copyCommandBuffer.copyBuffer(
                srcBuffer, dstBuffer,
                vk::BufferCopy { srcOffset, 0, copySize });
            return dstBuffer;
        })
        | std::ranges::to<std::vector>();
}

vk_gltf_viewer::gltf::AssetResources::ResourceBytes::ResourceBytes(
    const fastgltf::Asset &asset,
    const std::filesystem::path &assetDir
) : bufferBytes { createBufferBytes(asset, assetDir) },
    images { createImages(asset, assetDir) } { }

auto vk_gltf_viewer::gltf::AssetResources::ResourceBytes::getBufferViewBytes(
    const fastgltf::BufferView &bufferView
) const noexcept -> std::span<const std::uint8_t> {
    return std::visit([&](std::span<const std::uint8_t> bytes) {
        return bytes.subspan(bufferView.byteOffset, bufferView.byteLength);
    }, bufferBytes[bufferView.bufferIndex]);
}

auto vk_gltf_viewer::gltf::AssetResources::ResourceBytes::createBufferBytes(
    const fastgltf::Asset &asset,
    const std::filesystem::path &assetDir
) const -> decltype(bufferBytes) {
    return asset.buffers
        | transform([&](const fastgltf::Buffer &buffer) {
            return visit(fastgltf::visitor {
                [](const fastgltf::sources::Array &array) -> std::variant<std::span<const std::uint8_t>, std::vector<std::uint8_t>> {
                    return array.bytes;
                },
                [&](const fastgltf::sources::URI &uri) -> std::variant<std::span<const std::uint8_t>, std::vector<std::uint8_t>> {
                    if (!uri.uri.isLocalPath()) throw std::runtime_error { "Non-local source URI not supported." };

                    std::ifstream file { assetDir / uri.uri.fspath(), std::ios::binary };
                    if (!file) throw std::runtime_error { std::format("Failed to open file: {} (error code={})", strerror(errno), errno) };

                    // Determine file size.
                    file.seekg(0, std::ios::end);
                    const std::size_t fileSize = file.tellg();

                    std::vector<std::uint8_t> data(fileSize - uri.fileByteOffset);
                    file.seekg(uri.fileByteOffset);
                    file.read(reinterpret_cast<char*>(data.data()), data.size());

                    return { std::move(data) };
                },
                [](const auto &) -> std::variant<std::span<const std::uint8_t>, std::vector<std::uint8_t>> {
                    throw std::runtime_error { "Unsupported source data type" };
                },
            }, buffer.data);
        })
        | std::ranges::to<std::vector>();
}

auto vk_gltf_viewer::gltf::AssetResources::ResourceBytes::createImages(
    const fastgltf::Asset& asset,
    const std::filesystem::path& assetDir
) const -> decltype(images) {
    const fastgltf::visitor visitor{
        [](const fastgltf::sources::Array& array) -> io::StbDecoder<std::uint8_t>::DecodeResult {
            // TODO: handle MimeType::None correctly.
            if (array.mimeType == fastgltf::MimeType::JPEG || array.mimeType == fastgltf::MimeType::PNG ||
                array.mimeType == fastgltf::MimeType::None) {
                return io::StbDecoder<std::uint8_t>::fromMemory(std::span { array.bytes }, 4);
            }
            throw std::runtime_error { "Unsupported image MIME type" };
        },
        [&](const fastgltf::sources::URI& uri) -> io::StbDecoder<std::uint8_t>::DecodeResult {
            if (!uri.uri.isLocalPath()) throw std::runtime_error { "Non-local source URI not supported." };

            // TODO: handle MimeType::None correctly.
            if (uri.mimeType == fastgltf::MimeType::JPEG || uri.mimeType == fastgltf::MimeType::PNG ||
                uri.mimeType == fastgltf::MimeType::None) {
                return io::StbDecoder<std::uint8_t>::fromFile((assetDir / uri.uri.fspath()).string().c_str(), 4);
            }
            throw std::runtime_error { "Unsupported image MIME type" };
        },
        [&](const fastgltf::sources::BufferView& bufferView) -> io::StbDecoder<std::uint8_t>::DecodeResult {
            if (bufferView.mimeType == fastgltf::MimeType::JPEG || bufferView.mimeType == fastgltf::MimeType::PNG) {
                return io::StbDecoder<std::uint8_t>::fromMemory(getBufferViewBytes(asset.bufferViews[bufferView.bufferViewIndex]), 4);
            }
            throw std::runtime_error { "Unsupported image MIME type" };
        },
        [](const auto&) -> io::StbDecoder<std::uint8_t>::DecodeResult {
            throw std::runtime_error { "Unsupported source data type" };
        },
    };

    if (asset.images.empty()) return {};

    std::vector<io::StbDecoder<std::uint8_t>::DecodeResult> images(asset.images.size());
    std::transform(std::execution::par_unseq, asset.images.begin(), asset.images.end(), images.begin(), [&](const fastgltf::Image& image) {
        return visit(visitor, image.data);
    });

    return images;
}

vk_gltf_viewer::gltf::AssetResources::AssetResources(
    const fastgltf::Asset &asset,
    const std::filesystem::path &assetDir,
    const vulkan::Gpu &gpu,
    const Config &config
) : AssetResources { asset, ResourceBytes { asset, assetDir }, gpu, config } { }

vk_gltf_viewer::gltf::AssetResources::AssetResources(
    const fastgltf::Asset &asset,
    const ResourceBytes &resourceBytes,
    const vulkan::Gpu &gpu,
    const Config &config
) : asset { asset },
    primitiveInfos { createPrimitiveInfos(asset) },
    defaultSampler { createDefaultSampler(gpu.device) },
    images { createImages(resourceBytes, gpu.allocator) },
    imageViews { createImageViews(gpu.device) },
    samplers { createSamplers(gpu.device) },
    materialBuffer { createMaterialBuffer(gpu.allocator) } {
    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo {
        {},
        gpu.queueFamilies.transfer,
    } };
    vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
        stageImages(resourceBytes, gpu.allocator, cb);
        stageMaterials(gpu.allocator, cb);
        stagePrimitiveAttributeBuffers(resourceBytes, gpu, cb);
        stagePrimitiveIndexedAttributeMappingBuffers(IndexedAttribute::Texcoord, gpu, cb);
        stagePrimitiveIndexedAttributeMappingBuffers(IndexedAttribute::Color, gpu, cb);
        stagePrimitiveTangentBuffers(resourceBytes, gpu, cb);
        stagePrimitiveIndexBuffers(resourceBytes, gpu.allocator, cb, config.supportUint8Index);

        releaseResourceQueueFamilyOwnership(gpu.queueFamilies, cb);
    });

    gpu.queues.transfer.waitIdle();
    stagingBuffers.clear();
}

auto vk_gltf_viewer::gltf::AssetResources::createPrimitiveInfos(
    const fastgltf::Asset &asset
) const -> decltype(primitiveInfos) {
    std::unordered_map<const fastgltf::Primitive*, PrimitiveInfo> primitiveInfos;
    for (const auto &[nodeIndex, node] : asset.nodes | ranges::views::enumerate){
        if (!node.meshIndex) continue;

        for (const fastgltf::Primitive &primitive : asset.meshes[*node.meshIndex].primitives){
            primitiveInfos.try_emplace(
                &primitive,
                [&]() -> std::optional<std::uint32_t> {
                    if (primitive.materialIndex) return static_cast<std::uint32_t>(*primitive.materialIndex);
                    return std::nullopt;
                }());
        }
    }

    return primitiveInfos;
}

auto vk_gltf_viewer::gltf::AssetResources::createDefaultSampler(
    const vk::raii::Device &device
) const -> decltype(defaultSampler) {
    return { device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        {}, {}, {},
        {},
        true, 16.f,
        {}, {},
        {}, vk::LodClampNone,
    } };
}

auto vk_gltf_viewer::gltf::AssetResources::createImages(
    const ResourceBytes &resourceBytes,
    vma::Allocator allocator
) const -> decltype(images) {
    // Base color and emissive texture must be in SRGB format.
    // Therefore, first traverse the asset and fetch the image index that must be in vk::Format::eR8G8B8A8Srgb.
    std::unordered_set<std::size_t> srgbImageIndices;
    for (const fastgltf::Material &material : asset.materials) {
        if (const auto &baseColorTexture = material.pbrData.baseColorTexture) {
            srgbImageIndices.emplace(asset.textures[baseColorTexture->textureIndex].imageIndex.value());
        }
        if (const auto &emissiveTexture = material.emissiveTexture) {
            srgbImageIndices.emplace(asset.textures[emissiveTexture->textureIndex].imageIndex.value());
        }
    }

    return resourceBytes.images
        | ranges::views::enumerate
        | transform([&](const auto &indexedResult) {
            const auto &[imageIndex, decodeResult] = indexedResult;
            return vku::AllocatedImage { allocator, vk::ImageCreateInfo {
                {},
                vk::ImageType::e2D,
                srgbImageIndices.contains(imageIndex) ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm,
                vk::Extent3D { decodeResult.width, decodeResult.height, 1 },
                vku::Image::maxMipLevels({ decodeResult.width, decodeResult.height }), 1,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
            } };
        })
        | std::ranges::to<std::vector>();
}

auto vk_gltf_viewer::gltf::AssetResources::createImageViews(
    const vk::raii::Device &device
) const -> decltype(imageViews) {
    return images
        | transform([&](const vku::Image &image) {
            return vk::raii::ImageView { device, image.getViewCreateInfo() };
        })
        | std::ranges::to<std::vector>();
}

auto vk_gltf_viewer::gltf::AssetResources::createSamplers(
    const vk::raii::Device &device
) const -> decltype(samplers) {
    return asset.samplers
        | transform([&](const fastgltf::Sampler &assetSampler) {
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

            // TODO: how can map OpenGL filter to Vulkan corresponds?
            // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSamplerCreateInfo.html
            constexpr auto applyFilter = [](bool mag, vk::SamplerCreateInfo &createInfo, fastgltf::Filter filter) -> void {
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

            vk::SamplerCreateInfo createInfo {
                {},
                {}, {}, {},
                convertSamplerAddressMode(assetSampler.wrapS), convertSamplerAddressMode(assetSampler.wrapT), {},
                {},
                true, 16.f,
                {}, {},
                {}, vk::LodClampNone,
            };
            if (assetSampler.magFilter) applyFilter(true, createInfo, *assetSampler.magFilter);
            if (assetSampler.minFilter) applyFilter(false, createInfo, *assetSampler.minFilter);

            // For best performance, all address mode should be the same.
            // https://developer.arm.com/documentation/101897/0302/Buffers-and-textures/Texture-and-sampler-descriptors
            if (createInfo.addressModeU == createInfo.addressModeV) {
                createInfo.addressModeW = createInfo.addressModeU;
            }

            return vk::raii::Sampler { device, createInfo };
        })
        | std::ranges::to<std::vector>();
}

auto vk_gltf_viewer::gltf::AssetResources::createTextures() const -> decltype(textures) {
    return asset.textures
        | transform([&](const fastgltf::Texture &texture) {
            return vk::DescriptorImageInfo {
                [&]() {
                    if (texture.samplerIndex) return *samplers[*texture.samplerIndex];
                    return *defaultSampler;
                }(),
                imageViews[*texture.imageIndex],
                vk::ImageLayout::eShaderReadOnlyOptimal,
            };
        })
        | std::ranges::to<std::vector>();
}

auto vk_gltf_viewer::gltf::AssetResources::createMaterialBuffer(
    vma::Allocator allocator
) const -> decltype(materialBuffer) {
    if (asset.materials.empty()) return std::nullopt;
    return std::optional<vku::AllocatedBuffer> { std::in_place, allocator, vk::BufferCreateInfo {
        {},
        sizeof(GpuMaterial) * asset.materials.size(),
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    } };
}

auto vk_gltf_viewer::gltf::AssetResources::stageImages(
    const ResourceBytes &resourceBytes,
    vma::Allocator allocator,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    if (images.empty()) return;

    const auto &[stagingBuffer, copyOffsets] = createCombinedStagingBuffer(
        allocator,
        resourceBytes.images | transform([](const auto &image) { return image.asSpan(); }));

    // 1. Change image[mipLevel=0] layouts to vk::ImageLayout::eTransferDstOptimal for staging.
    copyCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
        {}, {}, {},
        images
            | transform([](vk::Image image) {
                return vk::ImageMemoryBarrier {
                    {}, vk::AccessFlagBits::eTransferWrite,
                    {}, vk::ImageLayout::eTransferDstOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                };
            })
            | std::ranges::to<std::vector>());

    // 2. Copy image data from staging buffer to images.
    for (const auto &[image, copyOffset] : zip(images, copyOffsets)) {
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

auto vk_gltf_viewer::gltf::AssetResources::stageMaterials(
    vma::Allocator allocator,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    if (!materialBuffer) return;

    const auto gpuMaterials
        = asset.materials
        | transform([&](const fastgltf::Material &material) {
            GpuMaterial gpuMaterial {
                .baseColorFactor = glm::gtc::make_vec4(material.pbrData.baseColorFactor.data()),
                .metallicFactor = material.pbrData.metallicFactor,
                .roughnessFactor = material.pbrData.roughnessFactor,
                .emissiveFactor = glm::gtc::make_vec3(material.emissiveFactor.data()),
                .doubleSided = material.doubleSided,
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
        });

    const vk::Buffer stagingBuffer = stagingBuffers.emplace_back(
        allocator, std::from_range, gpuMaterials, vk::BufferUsageFlagBits::eTransferSrc);
    copyCommandBuffer.copyBuffer(
        stagingBuffer, *materialBuffer,
        vk::BufferCopy { 0, 0, materialBuffer->size });
}

auto vk_gltf_viewer::gltf::AssetResources::stagePrimitiveAttributeBuffers(
    const ResourceBytes &resourceBytes,
    const vulkan::Gpu &gpu,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    const auto primitives = asset.meshes | transform(&fastgltf::Mesh::primitives) | join;

    // Get buffer view indices that are used in primitive attributes.
    const std::unordered_set attributeBufferViewIndices
        = primitives
        | transform([](const fastgltf::Primitive &primitive) {
            return primitive.attributes | values;
        })
        | join
        | transform([&](std::size_t accessorIndex) {
            const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];

            // Check accessor validity.
            if (accessor.sparse) throw std::runtime_error { "Sparse attribute accessor not supported" };
            if (accessor.normalized) throw std::runtime_error { "Normalized attribute accessor not supported" };
            if (!accessor.bufferViewIndex) throw std::runtime_error { "Missing attribute accessor buffer view index" };

            return *accessor.bufferViewIndex;
        })
        | std::ranges::to<std::unordered_set>();

    // Ordered sequence of (bufferViewIndex, bufferViewBytes) pairs.
    const std::vector attributeBufferViewBytes
        = attributeBufferViewIndices
        | transform([&](std::size_t bufferViewIndex) {
            return std::pair { bufferViewIndex, resourceBytes.getBufferViewBytes(asset.bufferViews[bufferViewIndex]) };
        })
        | std::ranges::to<std::vector>();

    // Create the combined staging buffer that contains all attributeBufferViewBytes.
    const auto &[stagingBuffer, copyOffsets] = createCombinedStagingBuffer(gpu.allocator, attributeBufferViewBytes | values);

    // Create device local buffers for each attributeBufferViewBytes, and record copy commands to the copyCommandBuffer.
    attributeBuffers = createStagingDstBuffers(
        gpu.allocator,
        stagingBuffer,
        ranges::views::zip_transform([](std::span<const std::uint8_t> bufferViewBytes, vk::DeviceSize srcOffset) {
            return std::tuple {
                srcOffset,
                bufferViewBytes.size(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            };
        }, attributeBufferViewBytes | values, copyOffsets),
        copyCommandBuffer);

    // Hashmap that can get buffer device address by corresponding buffer view index.
    const std::unordered_map bufferDeviceAddressMappings
        = zip(attributeBufferViewBytes | keys, attributeBuffers | transform([&](vk::Buffer buffer) { return gpu.device.getBufferAddress({ buffer }); }))
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

            if (attributeName == "POSITION") {
                primitiveInfo.positionInfo = getAttributeBufferInfo();
                primitiveInfo.drawCount = accessor.count;
            }
            // For std::optional, they must be initialized before being accessed.
            else if (attributeName == "NORMAL") {
                primitiveInfo.normalInfo.emplace(getAttributeBufferInfo());
            }
            else if (attributeName == "TANGENT") {
                primitiveInfo.tangentInfo.emplace(getAttributeBufferInfo());
            }
            // Otherwise, attributeName has form of <TEXCOORD_i> or <COLOR_i>.
            else if (constexpr auto prefix = "TEXCOORD_"sv; attributeName.starts_with(prefix)) {
                primitiveInfo.texcoordInfos[parseIndex(attributeName.substr(prefix.size()))] = getAttributeBufferInfo();
            }
            else if (constexpr auto prefix = "COLOR_"sv; attributeName.starts_with(prefix)) {
                primitiveInfo.colorInfos[parseIndex(attributeName.substr(prefix.size()))] = getAttributeBufferInfo();
            }
        }
    }
}

auto vk_gltf_viewer::gltf::AssetResources::stagePrimitiveIndexedAttributeMappingBuffers(
    IndexedAttribute attributeType,
    const vulkan::Gpu &gpu,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    const std::vector attributeBufferInfos
        = primitiveInfos
        | values
        | transform([attributeType](const PrimitiveInfo &primitiveInfo) {
            const auto &targetAttributeInfoMap = [=]() -> decltype(auto) {
                switch (attributeType) {
                    case IndexedAttribute::Texcoord: return primitiveInfo.texcoordInfos;
                    case IndexedAttribute::Color: return primitiveInfo.colorInfos;
                }
                std::unreachable(); // Invalid attributeType: must be Texcoord or Color
            }();

            return iota(0U, targetAttributeInfoMap.size())
                | transform([&](std::size_t i) {
                    return ranges::value_or(targetAttributeInfoMap, i, {});
                })
                | std::ranges::to<std::vector>();
        })
        | std::ranges::to<std::vector>();

    // If there's no attributeBufferInfo to process, skip processing.
    const std::size_t attributeBufferInfoCount = std::transform_reduce(
        attributeBufferInfos.begin(), attributeBufferInfos.end(),
        std::size_t { 0 }, std::plus{}, [](const auto& v) { return v.size(); });
    if (attributeBufferInfoCount == 0) return;

    const std::vector addressSegments
        = attributeBufferInfos
        | transform([](const auto &attributeBufferInfos) {
            return attributeBufferInfos
                | transform(&PrimitiveInfo::AttributeBufferInfo::address)
                | std::ranges::to<std::vector>();
        })
        | std::ranges::to<std::vector>();
    const std::vector byteStrideSegments
        = attributeBufferInfos
        | transform([](const auto &attributeBufferInfos) {
            return attributeBufferInfos
                | transform(&PrimitiveInfo::AttributeBufferInfo::byteStride)
                | std::ranges::to<std::vector>();
        })
        | std::ranges::to<std::vector>();

    const auto &[bufferPtrStagingBuffer, bufferPtrCopyOffsets] = createCombinedStagingBuffer(gpu.allocator, addressSegments);
    const auto &[stridesStagingBuffer, strideCopyOffsets] = createCombinedStagingBuffer(gpu.allocator, byteStrideSegments);
    const auto &[bufferPtrsBuffer, byteStridesBuffer] = indexedAttributeMappingBuffers.try_emplace(
        attributeType,
        createStagingDstBuffer(
            gpu.allocator, bufferPtrStagingBuffer,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            copyCommandBuffer),
        createStagingDstBuffer(
            gpu.allocator, stridesStagingBuffer,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            copyCommandBuffer)).first /* iterator */ ->second /* std::pair<vku::AllocatedBuffer, vku::AllocatedBuffer> */;

    const vk::DeviceAddress pBufferPtrsBuffer = gpu.device.getBufferAddress({ bufferPtrsBuffer.buffer }),
                            pByteStridesBuffer = gpu.device.getBufferAddress({ byteStridesBuffer.buffer });

    for (auto &&[primitiveInfo, bufferPtrCopyOffset, strideCopyOffset] : zip(primitiveInfos | values, bufferPtrCopyOffsets, strideCopyOffsets)) {
        primitiveInfo.indexedAttributeMappingInfos.try_emplace(
            attributeType,
            pBufferPtrsBuffer + bufferPtrCopyOffset,
            pByteStridesBuffer + strideCopyOffset);
    }
}

auto vk_gltf_viewer::gltf::AssetResources::stagePrimitiveTangentBuffers(
    const ResourceBytes &resourceBytes,
    const vulkan::Gpu &gpu,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    std::vector missingTangentMeshes
        = primitiveInfos
        | filter([&](const auto &keyValue) {
            const auto &[pPrimitive, primitiveInfo] = keyValue;

            // Skip if primitive already has a tangent attribute.
            if (primitiveInfo.tangentInfo) return false;
            // Skip if primitive doesn't have a material.
            else if (const auto &materialIndex = pPrimitive->materialIndex; !materialIndex) return false;
            // Skip if primitive doesn't have a normal texture.
            else return asset.materials[*materialIndex].normalTexture.has_value();
        })
        | transform([&](const auto &keyValue) {
            const auto &[pPrimitive, primitiveInfo] = keyValue;

            // Validate constriant for MikktSpaceInterface.
            if (auto normalIt = pPrimitive->findAttribute("NORMAL"); normalIt == pPrimitive->attributes.end()) {
                throw std::runtime_error { "Missing NORMAL attribute" };
            }
            else if (auto texcoordIt = pPrimitive->findAttribute(std::format("TEXCOORD_{}", asset.materials[*pPrimitive->materialIndex].normalTexture->texCoordIndex));
                texcoordIt == pPrimitive->attributes.end()) {
                throw std::runtime_error { "Missing TEXCOORD attribute" };
            }
            else if (!pPrimitive->indicesAccessor) {
                throw std::runtime_error { "Missing indices accessor" };
            }
            else return algorithm::MikktSpaceMesh {
                asset,
                asset.accessors[*pPrimitive->indicesAccessor],
                asset.accessors[pPrimitive->findAttribute("POSITION")->second],
                asset.accessors[normalIt->second],
                asset.accessors[texcoordIt->second],
                [&](const fastgltf::Buffer &buffer) {
                    const std::size_t bufferIndex = &buffer - asset.buffers.data();
                    return std::visit([](std::span<const std::uint8_t> bytes) {
                        return as_bytes(bytes).data();
                    }, resourceBytes.bufferBytes[bufferIndex]);
                },
            };
        })
        | std::ranges::to<std::vector>();
    if (missingTangentMeshes.empty()) return; // Skip if there's no missing tangent mesh.

    std::for_each(std::execution::par_unseq, missingTangentMeshes.begin(), missingTangentMeshes.end(), [&](algorithm::MikktSpaceMesh& mesh) {
        SMikkTSpaceInterface* const pInterface
            = [indexType = mesh.indicesAccessor.componentType]() -> SMikkTSpaceInterface* {
                switch (indexType) {
                    case fastgltf::ComponentType::UnsignedShort:
                        return &algorithm::mikktSpaceInterface<std::uint16_t>;
                    case fastgltf::ComponentType::UnsignedInt:
                        return &algorithm::mikktSpaceInterface<std::uint32_t>;
                    default:
                        throw std::runtime_error{ "Unsupported index type" };
                }
            }();
        if (const SMikkTSpaceContext context{ pInterface, &mesh }; !genTangSpaceDefault(&context)) {
            throw std::runtime_error{ "Failed to generate tangent attributes" };
        }
    });

    const auto &[stagingBuffer, copyOffsets] = createCombinedStagingBuffer(
        gpu.allocator,
        missingTangentMeshes | transform(&algorithm::MikktSpaceMesh::tangents));
    tangentBuffer.emplace(
        createStagingDstBuffer(
            gpu.allocator, stagingBuffer,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            copyCommandBuffer));
    const vk::DeviceAddress pTangentBuffer = gpu.device.getBufferAddress({ tangentBuffer->buffer });

    for (auto &&[primitiveInfo, copyOffset] : zip(primitiveInfos | values, copyOffsets)) {
        primitiveInfo.tangentInfo.emplace(pTangentBuffer + copyOffset, 16);
    }
}

auto vk_gltf_viewer::gltf::AssetResources::stagePrimitiveIndexBuffers(
    const ResourceBytes &resourceBytes,
    vma::Allocator allocator,
    vk::CommandBuffer copyCommandBuffer,
    bool supportUint8Index
) -> void {
    // Primitive that are contains an indices accessor.
    auto indexedPrimitives = asset.meshes
        | transform(&fastgltf::Mesh::primitives)
        | join
        | filter([](const fastgltf::Primitive &primitive) { return primitive.indicesAccessor.has_value(); });

    // Index data is either
    // - span of the buffer view region, or
    // - vector of uint16 indices if accessor have unsigned byte component and native uint8 index is not supported.
    std::unordered_map<vk::IndexType, std::vector<std::pair<const fastgltf::Primitive*, std::variant<std::span<const std::uint8_t>, std::vector<std::uint16_t>>>>> indexBufferBytesByType;

    // Get buffer view bytes from indexedPrimtives and group them by index type.
    for (const fastgltf::Primitive &primitive : indexedPrimitives) {
        const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];

        // Check accessor validity.
        if (accessor.sparse) throw std::runtime_error { "Sparse indices accessor not supported" };
        if (accessor.normalized) throw std::runtime_error { "Normalized indices accessor not supported" };
        if (!accessor.bufferViewIndex) throw std::runtime_error { "Missing indices accessor buffer view index" };

        // Vulkan does not support interleaved index buffer.
        const std::size_t componentByteSize = getElementByteSize(accessor.type, accessor.componentType);
        bool isIndexInterleaved = false;
        if (const auto& byteStride = asset.bufferViews[*accessor.bufferViewIndex].byteStride; byteStride) {
            isIndexInterleaved = *byteStride != componentByteSize;
        }
        if (isIndexInterleaved) throw std::runtime_error { "Interleaved index buffer not supported" };

        switch (accessor.componentType) {
            case fastgltf::ComponentType::UnsignedByte: {
                if (supportUint8Index) {
                    indexBufferBytesByType[vk::IndexType::eUint8KHR].emplace_back(
                        &primitive,
                        resourceBytes.getBufferViewBytes(asset.bufferViews[*accessor.bufferViewIndex])
                            .subspan(accessor.byteOffset, accessor.count * componentByteSize));
                }
                else {
                    // Make vector of uint16 indices.
                    std::vector<std::uint16_t> indices(accessor.count);
                    iterateAccessorWithIndex<std::uint8_t>(asset, accessor, [&](std::size_t i, std::uint8_t index) {
                        indices[i] = index; // Index converted from uint8 to uint16.
                    }, [&](const fastgltf::Buffer &buffer) {
                        const std::size_t bufferIndex = &buffer - asset.buffers.data();
                        return std::visit([](std::span<const std::uint8_t> bytes) {
                            return as_bytes(bytes).data();
                        }, resourceBytes.bufferBytes[bufferIndex]);
                    });

                    indexBufferBytesByType[vk::IndexType::eUint16].emplace_back(
                        &primitive,
                        std::move(indices));
                }
                break;
            }
            case fastgltf::ComponentType::UnsignedShort:
                indexBufferBytesByType[vk::IndexType::eUint16].emplace_back(
                    &primitive,
                    resourceBytes.getBufferViewBytes(asset.bufferViews[*accessor.bufferViewIndex])
                        .subspan(accessor.byteOffset, accessor.count * componentByteSize));
                break;
            case fastgltf::ComponentType::UnsignedInt:
                indexBufferBytesByType[vk::IndexType::eUint32].emplace_back(
                    &primitive,
                    resourceBytes.getBufferViewBytes(asset.bufferViews[*accessor.bufferViewIndex])
                        .subspan(accessor.byteOffset, accessor.count * componentByteSize));
                break;
            default:
                throw std::runtime_error { "Unsupported index type: index must be either unsigned byte/short/int." };
        }
    }

    // Combine index data into a single staging buffer, and create GPU local buffers for each index data. Record copy
    // commands to copyCommandBuffer.
    indexBuffers = indexBufferBytesByType
        | transform([&](const auto &keyValue) {
            const auto &[indexType, bufferBytes] = keyValue;
            const auto &[stagingBuffer, copyOffsets] = createCombinedStagingBuffer(
                allocator,
                bufferBytes | values | transform([](const auto &variant) {
                    return std::visit(fastgltf::visitor {
                        [](std::span<const std::uint8_t> bufferBytes) { return as_bytes(bufferBytes); },
                        [](std::span<const std::uint16_t> indices) { return as_bytes(indices); },
                    }, variant);
                }));
            auto indexBuffer = createStagingDstBuffer(allocator, stagingBuffer, vk::BufferUsageFlagBits::eIndexBuffer, copyCommandBuffer);

            for (auto [pPrimitive, offset] : zip(bufferBytes | keys, copyOffsets)) {
                PrimitiveInfo &primitiveInfo = primitiveInfos[pPrimitive];
                primitiveInfo.indexInfo.emplace(offset, indexType);
                primitiveInfo.drawCount = asset.accessors[*pPrimitive->indicesAccessor].count;
            }

            return std::pair { indexType, std::move(indexBuffer) };
        })
        | std::ranges::to<std::unordered_map>();
}

auto vk_gltf_viewer::gltf::AssetResources::releaseResourceQueueFamilyOwnership(
    const vulkan::Gpu::QueueFamilies &queueFamilies,
    vk::CommandBuffer commandBuffer
) const -> void {
    if (queueFamilies.transfer == queueFamilies.graphicsPresent) return;

    std::vector<vk::Buffer> targetBuffers { std::from_range, attributeBuffers };
    if (materialBuffer) targetBuffers.emplace_back(*materialBuffer);
    targetBuffers.append_range(indexBuffers | values);
    for (const auto &[bufferPtrsBuffer, byteStridesBuffer] : indexedAttributeMappingBuffers | values) {
        targetBuffers.emplace_back(bufferPtrsBuffer);
        targetBuffers.emplace_back(byteStridesBuffer);
    }
    if (tangentBuffer) targetBuffers.emplace_back(*tangentBuffer);

    std::vector<vk::Image> targetImages { std::from_range, images };

    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
        {}, {},
        targetBuffers
            | transform([&](vk::Buffer buffer) {
                return vk::BufferMemoryBarrier {
                    vk::AccessFlagBits::eTransferWrite, {},
                    queueFamilies.transfer, queueFamilies.graphicsPresent,
                    buffer,
                    0, vk::WholeSize,
                };
            })
            | std::ranges::to<std::vector>(),
        targetImages
            | transform([&](vk::Image image) {
                return vk::ImageMemoryBarrier {
                    vk::AccessFlagBits::eTransferWrite, {},
                    {}, {},
                    queueFamilies.transfer, queueFamilies.graphicsPresent,
                    image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                };
            })
            | std::ranges::to<std::vector>());
}