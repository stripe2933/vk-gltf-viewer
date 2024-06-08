module;

#include <compare>
#include <format>
#include <list>
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fastgltf/core.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetResources;
import :helpers;

[[nodiscard]] auto createStagingDstBuffer(
    vma::Allocator allocator,
    const vku::Buffer &srcBuffer,
    vk::BufferUsageFlags dstBufferUsage,
    vk::CommandBuffer copyCommandBuffer
) -> vku::AllocatedBuffer {
    vku::AllocatedBuffer dstBuffer {
        allocator,
        vk::BufferCreateInfo {
            {},
            srcBuffer.size,
            dstBufferUsage | vk::BufferUsageFlagBits::eTransferDst,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        },
    };
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
        | std::views::transform([&](const auto &copyInfo) {
            const auto [srcOffset, copySize, dstBufferUsage] = copyInfo;
            vku::AllocatedBuffer dstBuffer {
                allocator,
                vk::BufferCreateInfo {
                    {},
                    copySize,
                    dstBufferUsage | vk::BufferUsageFlagBits::eTransferDst,
                },
                vma::AllocationCreateInfo {
                    {},
                    vma::MemoryUsage::eAutoPreferDevice,
                },
            };
            copyCommandBuffer.copyBuffer(
                srcBuffer, dstBuffer,
                vk::BufferCopy { srcOffset, 0, copySize });
            return dstBuffer;
        })
        | std::ranges::to<std::vector<vku::AllocatedBuffer>>();
}

vk_gltf_viewer::gltf::AssetResources::ResourceBytes::ResourceBytes(
    const fastgltf::Asset &asset,
    const std::filesystem::path &assetDir
) {
    const fastgltf::visitor assetBufferSourceVisitor {
        [](const fastgltf::sources::Array &array) -> std::span<const std::uint8_t> {
            return array.bytes;
        },
        [&](const fastgltf::sources::URI &uri) -> std::span<const std::uint8_t> {
            if (!uri.uri.isLocalPath()) throw std::runtime_error { "Non-local source URI not supported." };

            std::ifstream file { assetDir / uri.uri.fspath(), std::ios::binary };
            if (!file) throw std::runtime_error { std::format("Failed to open file: {} (error code={})", strerror(errno), errno) };

            // Determine file size.
            file.seekg(0, std::ios::end);
            const std::size_t fileSize = file.tellg();

            std::vector<std::uint8_t> data(fileSize - uri.fileByteOffset);
            file.seekg(uri.fileByteOffset);
            file.read(reinterpret_cast<char*>(data.data()), data.size());

            return externalBufferBytes.emplace_back(std::move(data));
        },
        [](const auto &) -> std::span<const std::uint8_t> {
            throw std::runtime_error { "Unsupported source data type" };
        },
    };

    for (const fastgltf::Buffer &buffer : asset.buffers) {
        bufferBytes.emplace_back(visit(assetBufferSourceVisitor, buffer.data));
    }
    // for (const fastgltf::Image &image : asset.images) {
    //     imageBytes.emplace_back(visit(assetSourceVisitor, image.data));
    // }
}

auto vk_gltf_viewer::gltf::AssetResources::ResourceBytes::getBufferViewBytes(
    const fastgltf::BufferView &bufferView
) const noexcept -> std::span<const std::uint8_t> {
    return bufferBytes[bufferView.bufferIndex].subspan(bufferView.byteOffset, bufferView.byteLength);
}

vk_gltf_viewer::gltf::AssetResources::AssetResources(
    const fastgltf::Asset &asset,
    const std::filesystem::path &assetDir,
    const vulkan::Gpu &gpu
) {
    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo {
        {},
        gpu.queueFamilies.graphicsPresent,
    } };
    vku::executeSingleCommand(
        *gpu.device, *transferCommandPool, gpu.queues.graphicsPresent,
        [&, resourceBytes = ResourceBytes { asset, assetDir }](vk::CommandBuffer cb) {
            setPrimitiveIndexData(asset, gpu, resourceBytes, cb);
            setPrimitiveAttributeData(asset, gpu, resourceBytes, cb);
        });

    gpu.queues.graphicsPresent.waitIdle();
    stagingBuffers.clear();
}

auto vk_gltf_viewer::gltf::AssetResources::setPrimitiveIndexData(
    const fastgltf::Asset &asset,
    const vulkan::Gpu &gpu,
    const ResourceBytes &resourceBytes,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    // Primitive that are contains an indices accessor.
    auto indexedPrimitives = asset.meshes
        | std::views::transform(&fastgltf::Mesh::primitives)
        | std::views::join
        | std::views::filter([](const fastgltf::Primitive &primitive) { return primitive.indicesAccessor.has_value(); });

    // Get buffer view bytes from indexedPrimtives and group them by index type.
    std::unordered_map<vk::IndexType, std::vector<std::pair<const fastgltf::Primitive*, std::span<const std::uint8_t>>>> indexBufferBytesByType;
    for (const fastgltf::Primitive &primitive : indexedPrimitives) {
        const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];

        // Check accessor validity.
        if (accessor.sparse) throw std::runtime_error { "Sparse indices accessor not supported" };
        if (accessor.normalized) throw std::runtime_error { "Normalized indices accessor not supported" };
        if (!accessor.bufferViewIndex) throw std::runtime_error { "Missing indices accessor buffer view index" };
        const std::size_t componentByteSize = getElementByteSize(accessor.type, accessor.componentType);
        // TODO: use monadic operation when fastgltf correctly support it.
        // const bool isIndexInterleaved
        //     = asset.bufferViews[*accessor.bufferViewIndex].byteStride
        //     .transform([=](std::size_t stride) { return stride != componentByteSize; })
        //     .value_or(false);
        bool isIndexInterleaved = false;
        if (const auto& byteStride = asset.bufferViews[*accessor.bufferViewIndex].byteStride; byteStride) {
            isIndexInterleaved = *byteStride != componentByteSize;
        }
        if (isIndexInterleaved) throw std::runtime_error { "Interleaved index buffer not supported" };

        const vk::IndexType indexType = [&]() {
            switch (accessor.componentType) {
                case fastgltf::ComponentType::UnsignedShort: return vk::IndexType::eUint16;
                case fastgltf::ComponentType::UnsignedInt: return vk::IndexType::eUint32;
                default: throw std::runtime_error { "Unsupported index type" };
            }
        }();
        indexBufferBytesByType[indexType].emplace_back(
            &primitive,
            resourceBytes.getBufferViewBytes(asset.bufferViews[*accessor.bufferViewIndex])
                .subspan(accessor.byteOffset, accessor.count * componentByteSize));
    }

    // Create combined staging buffers and GPU local buffers for each indexBufferBytes, and record copy commands to the
    // copyCommandBuffer.
    indexBuffers = indexBufferBytesByType
        | std::views::transform([&](const auto &keyValue) {
            const auto &[indexType, bufferBytes] = keyValue;
            const auto &[stagingBuffer, copyOffsets] = createCombinedStagingBuffer(gpu.allocator, bufferBytes | std::views::values);
            auto indexBuffer = createStagingDstBuffer(gpu.allocator, stagingBuffer, vk::BufferUsageFlagBits::eIndexBuffer, copyCommandBuffer);

            for (auto [pPrimitive, offset] : std::views::zip(bufferBytes | std::views::keys, copyOffsets)) {
                primitiveData[pPrimitive].indexInfo = {
                    .offset = offset,
                    .type = indexType,
                    .drawCount = static_cast<std::uint32_t>(asset.accessors[*pPrimitive->indicesAccessor].count),
                };
            }

            return std::pair { indexType, std::move(indexBuffer) };
        })
        | std::ranges::to<std::unordered_map<vk::IndexType, vku::AllocatedBuffer>>();
}

auto vk_gltf_viewer::gltf::AssetResources::setPrimitiveAttributeData(
    const fastgltf::Asset &asset,
    const vulkan::Gpu &gpu,
    const ResourceBytes &resourceBytes,
    vk::CommandBuffer copyCommandBuffer
) -> void {
    const auto primitives = asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join;

    // Get buffer view indices that are used in primitive attributes.
    const std::unordered_set attributeBufferViewIndices
        = primitives
        | std::views::transform([](const fastgltf::Primitive &primitive) {
            return primitive.attributes | std::views::values;
        })
        | std::views::join
        | std::views::transform([&](std::size_t accessorIndex) {
            const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];

            // Check accessor validity.
            if (accessor.sparse) throw std::runtime_error { "Sparse attribute accessor not supported" };
            if (accessor.normalized) throw std::runtime_error { "Normalized attribute accessor not supported" };
            if (!accessor.bufferViewIndex) throw std::runtime_error { "Missing attribute accessor buffer view index" };

            return *accessor.bufferViewIndex;
        })
        | std::ranges::to<std::unordered_set<std::size_t>>();

    // Ordered sequence of (bufferViewIndex, bufferViewBytes) pairs.
    const std::vector attributeBufferViewBytes
        = attributeBufferViewIndices
        | std::views::transform([&](std::size_t bufferViewIndex) {
            return std::pair { bufferViewIndex, resourceBytes.getBufferViewBytes(asset.bufferViews[bufferViewIndex]) };
        })
        | std::ranges::to<std::vector<std::pair<std::size_t, std::span<const std::uint8_t>>>>();

    // Create the combined staging buffer that contains all attributeBufferViewBytes.
    const auto &[stagingBuffer, copyOffsets] = createCombinedStagingBuffer(gpu.allocator, attributeBufferViewBytes | std::views::values);

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
        }, attributeBufferViewBytes | std::views::values, copyOffsets),
        copyCommandBuffer);

    // Hashmap that can get buffer by corresponding buffer view index.
    const std::unordered_map bufferMappings
        = std::views::zip(attributeBufferViewBytes | std::views::keys, attributeBuffers)
        | std::ranges::to<std::unordered_map<std::size_t, vk::Buffer>>();

    // Iterate over the primitives and set their attribute infos.
    for (const fastgltf::Primitive &primitive : primitives) {
        const fastgltf::Accessor &positionAccessor = asset.accessors[primitive.findAttribute("POSITION")->second],
                                 &normalAccessor = asset.accessors[primitive.findAttribute("NORMAL")->second];

        PrimitiveData &data = primitiveData[&primitive];
        data.positionInfo = {
            .address = gpu.device.getBufferAddress({ bufferMappings.at(*positionAccessor.bufferViewIndex) }) + positionAccessor.byteOffset,
            .byteStride = asset.bufferViews[*positionAccessor.bufferViewIndex].byteStride
                .value_or(getElementByteSize(positionAccessor.type, positionAccessor.componentType)),
        };
        data.normalInfo = {
            .address = gpu.device.getBufferAddress({ bufferMappings.at(*normalAccessor.bufferViewIndex) }) + normalAccessor.byteOffset,
            .byteStride = asset.bufferViews[*normalAccessor.bufferViewIndex].byteStride
                .value_or(getElementByteSize(normalAccessor.type, normalAccessor.componentType)),
        };
    }
}