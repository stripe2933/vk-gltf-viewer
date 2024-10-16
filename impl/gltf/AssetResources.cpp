module;

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <mikktspace.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetResources;

import std;
import thread_pool;
import :gltf.algorithm.MikktSpaceInterface;
import :helpers.fastgltf;
import :helpers.ranges;

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
    const AssetExternalBuffers &externalBuffers,
    const vulkan::Gpu &gpu,
    BS::thread_pool threadPool
) : asset { asset },
    gpu { gpu } {
    // Transfer the asset resources into the GPU using transfer queue.
    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
    const vk::raii::Fence transferFence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
        stageMaterials(cb);
        stagePrimitiveAttributeBuffers(externalBuffers, cb);
        stagePrimitiveIndexedAttributeMappingBuffers(IndexedAttribute::Texcoord, cb);
        stagePrimitiveIndexedAttributeMappingBuffers(IndexedAttribute::Color, cb);
        stagePrimitiveTangentBuffers(externalBuffers, cb, threadPool);
        stagePrimitiveIndexBuffers(externalBuffers, cb);
    }, *transferFence);
    if (vk::Result result = gpu.device.waitForFences(*transferFence, true, ~0ULL); result != vk::Result::eSuccess) {
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

auto vk_gltf_viewer::gltf::AssetResources::createMaterialBuffer() const -> vku::AllocatedBuffer {
    return { gpu.allocator, vk::BufferCreateInfo {
        {},
        sizeof(GpuMaterial) * (1 /* fallback material */ + asset.materials.size()),
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    } };
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
            attributeBuffers | std::views::transform([&](vk::Buffer buffer) {
                return gpu.device.getBufferAddress({ buffer });
            }))
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
                primitiveInfo.min = glm::make_vec3(get_if<std::pmr::vector<double>>(&accessor.min)->data());
                primitiveInfo.max = glm::make_vec3(get_if<std::pmr::vector<double>>(&accessor.max)->data());
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
                if (primitiveInfo.texcoordInfos.size() <= texcoordIndex) {
                    primitiveInfo.texcoordInfos.resize(texcoordIndex + 1);
                }
                primitiveInfo.texcoordInfos[texcoordIndex] = getAttributeBufferInfo();
            }
            else if (constexpr auto prefix = "COLOR_"sv; attributeName.starts_with(prefix)) {
                const std::size_t colorIndex = parseIndex(std::string_view { attributeName }.substr(prefix.size()));
                if (primitiveInfo.colorInfos.size() <= colorIndex) {
                    primitiveInfo.colorInfos.resize(colorIndex + 1);
                }
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
            switch (attributeType) {
                case IndexedAttribute::Texcoord: return as_bytes(std::span { primitiveInfo.texcoordInfos });
                case IndexedAttribute::Color: return as_bytes(std::span { primitiveInfo.colorInfos });
            }
            std::unreachable(); // Invalid attributeType: must be Texcoord or Color
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

    threadPool.submit_loop(std::size_t{ 0 }, missingTangentMeshes.size(), [&](std::size_t meshIndex) {
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

    // Get buffer view bytes from indexedPrimitives and group them by index type.
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
                    case fastgltf::ComponentType::UnsignedByte: return vk::IndexType::eUint8EXT;
                    case fastgltf::ComponentType::UnsignedShort: return vk::IndexType::eUint16;
                    case fastgltf::ComponentType::UnsignedInt: return vk::IndexType::eUint32;
                    default: throw std::runtime_error { "Unsupported index type: only Uint8EXT, Uint16 and Uint32 are supported." };
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