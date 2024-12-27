module;

#include <cassert>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetGpuBuffers;

import std;
import :helpers.fastgltf;
import :helpers.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return (__VA_ARGS__)(FWD(xs)...); }

std::vector<const fastgltf::Primitive*> vk_gltf_viewer::gltf::AssetGpuBuffers::createOrderedPrimitives() const {
    return asset.meshes
        | std::views::transform(&fastgltf::Mesh::primitives)
        | std::views::join
        | ranges::views::addressof
        | std::ranges::to<std::vector>();
}

auto vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveInfos() const -> std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> {
    return orderedPrimitives
        | ranges::views::enumerate
        | ranges::views::decompose_transform([](std::uint16_t primitiveIndex, const fastgltf::Primitive *pPrimitive) {
            return std::pair {
                pPrimitive,
                AssetPrimitiveInfo {
                    .index = primitiveIndex,
                    .materialIndex = to_optional(pPrimitive->materialIndex),
                },
            };
        })
        | std::ranges::to<std::unordered_map>();
}

vku::AllocatedBuffer vk_gltf_viewer::gltf::AssetGpuBuffers::createMaterialBuffer() {
    vku::AllocatedBuffer stagingBuffer = vku::MappedBuffer {
        gpu.allocator,
        std::from_range, ranges::views::concat(
            std::views::single(GpuMaterial{}), // Fallback material.
            asset.materials | std::views::transform([&](const fastgltf::Material& material) {
                GpuMaterial gpuMaterial {
                    .baseColorFactor = glm::gtc::make_vec4(material.pbrData.baseColorFactor.data()),
                    .metallicFactor = material.pbrData.metallicFactor,
                    .roughnessFactor = material.pbrData.roughnessFactor,
                    .emissiveFactor = glm::gtc::make_vec3(material.emissiveFactor.data()),
                    .alphaCutOff = material.alphaCutoff,
                };

                if (const auto& baseColorTexture = material.pbrData.baseColorTexture) {
                    gpuMaterial.baseColorTexcoordIndex = baseColorTexture->texCoordIndex;
                    gpuMaterial.baseColorTextureIndex = static_cast<std::int16_t>(baseColorTexture->textureIndex);
                }
                if (const auto& metallicRoughnessTexture = material.pbrData.metallicRoughnessTexture) {
                    gpuMaterial.metallicRoughnessTexcoordIndex = metallicRoughnessTexture->texCoordIndex;
                    gpuMaterial.metallicRoughnessTextureIndex = static_cast<std::int16_t>(metallicRoughnessTexture->textureIndex);
                }
                if (const auto& normalTexture = material.normalTexture) {
                    gpuMaterial.normalTexcoordIndex = normalTexture->texCoordIndex;
                    gpuMaterial.normalTextureIndex = static_cast<std::int16_t>(normalTexture->textureIndex);
                    gpuMaterial.normalScale = normalTexture->scale;
                }
                if (const auto& occlusionTexture = material.occlusionTexture) {
                    gpuMaterial.occlusionTexcoordIndex = occlusionTexture->texCoordIndex;
                    gpuMaterial.occlusionTextureIndex = static_cast<std::int16_t>(occlusionTexture->textureIndex);
                    gpuMaterial.occlusionStrength = occlusionTexture->strength;
                }
                if (const auto& emissiveTexture = material.emissiveTexture) {
                    gpuMaterial.emissiveTexcoordIndex = emissiveTexture->texCoordIndex;
                    gpuMaterial.emissiveTextureIndex = static_cast<std::int16_t>(emissiveTexture->textureIndex);
                }

                return gpuMaterial;
            })),
        gpu.isUmaDevice ? vk::BufferUsageFlagBits::eStorageBuffer : vk::BufferUsageFlagBits::eTransferSrc,
    }.unmap();

    if (gpu.isUmaDevice || vku::contains(gpu.allocator.getAllocationMemoryProperties(stagingBuffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
        return stagingBuffer;
    }

    vku::AllocatedBuffer dstBuffer{ gpu.allocator, vk::BufferCreateInfo {
        {},
        stagingBuffer.size,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    } };
    stagingInfos.emplace_back(
        std::move(stagingBuffer),
        dstBuffer,
        vk::BufferCopy{ 0, 0, dstBuffer.size });
    return dstBuffer;
}

vku::AllocatedBuffer vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveBuffer() {
    vku::AllocatedBuffer stagingBuffer = vku::MappedBuffer {
        gpu.allocator,
        std::from_range, orderedPrimitives | std::views::transform([this](const fastgltf::Primitive *pPrimitive) {
            const AssetPrimitiveInfo &primitiveInfo = primitiveInfos[pPrimitive];

            // If normal and tangent not presented (nullopt), it will use a faceted mesh renderer, and they will does not
            // dereference those buffers. Therefore, it is okay to pass nullptr into shaders
            const auto normalInfo = primitiveInfo.normalInfo.value_or(AssetPrimitiveInfo::AttributeBufferInfo{});
            const auto tangentInfo = primitiveInfo.tangentInfo.value_or(AssetPrimitiveInfo::AttributeBufferInfo{});

            return GpuPrimitive {
                .pPositionBuffer = primitiveInfo.positionInfo.address,
                .pNormalBuffer = normalInfo.address,
                .pTangentBuffer = tangentInfo.address,
                .pTexcoordAttributeMappingInfoBuffer = primitiveInfo.texcoordsInfo.pMappingBuffer,
                .pColorAttributeMappingInfoBuffer = 0ULL, // TODO: implement color attribute mapping.
                .positionByteStride = primitiveInfo.positionInfo.byteStride,
                .normalByteStride = normalInfo.byteStride,
                .tangentByteStride = tangentInfo.byteStride,
                .materialIndex
                    = primitiveInfo.materialIndex.transform([](std::size_t index) {
                        return 1U /* index 0 is reserved for the fallback material */ + static_cast<std::uint32_t>(index);
                    })
                    .value_or(0U),
            };
        }),
        gpu.isUmaDevice ? vk::BufferUsageFlagBits::eStorageBuffer : vk::BufferUsageFlagBits::eTransferSrc,
    }.unmap();

    if (gpu.isUmaDevice || vku::contains(gpu.allocator.getAllocationMemoryProperties(stagingBuffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
        return stagingBuffer;
    }

    vku::AllocatedBuffer dstBuffer{ gpu.allocator, vk::BufferCreateInfo {
        {},
        stagingBuffer.size,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    } };
    stagingInfos.emplace_back(
        std::move(stagingBuffer),
        dstBuffer,
        vk::BufferCopy{ 0, 0, dstBuffer.size });
    return dstBuffer;
}

void vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveIndexedAttributeMappingBuffers() {
    // Collect primitives that have any TEXCOORD attributes.
    const std::vector primitiveWithTexcoordAttributeInfos
        = primitiveInfos
        | std::views::values
        | std::views::filter([](const AssetPrimitiveInfo &primitiveInfo) { return !primitiveInfo.texcoordsInfo.attributeInfos.empty(); })
        | std::views::transform([](AssetPrimitiveInfo &primitiveInfo) { return std::tie(primitiveInfo, primitiveInfo.texcoordsInfo.attributeInfos); })
        | std::ranges::to<std::vector>();

    if (primitiveWithTexcoordAttributeInfos.empty()) {
        return;
    }

    auto [buffer, copyOffsets] = createCombinedStagingBuffer(
        gpu.allocator,
        primitiveWithTexcoordAttributeInfos | std::views::values,
        gpu.isUmaDevice
            ? vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
            : vk::BufferUsageFlagBits::eTransferSrc);

    if (!gpu.isUmaDevice && !vku::contains(gpu.allocator.getAllocationMemoryProperties(buffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
        vku::AllocatedBuffer dstBuffer { gpu.allocator, vk::BufferCreateInfo {
            {},
            buffer.size,
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        } };
        stagingInfos.emplace_back(
            std::move(buffer),
            dstBuffer,
            vk::BufferCopy { 0, 0, dstBuffer.size });
        buffer = std::move(dstBuffer);
    }

    const vk::DeviceAddress pIndexAttributeMappingBuffer = gpu.device.getBufferAddress({ buffer });
    for (auto &&[primitiveInfo, copyOffset] : std::views::zip(primitiveWithTexcoordAttributeInfos | std::views::keys, copyOffsets)) {
        primitiveInfo.texcoordsInfo.pMappingBuffer = pIndexAttributeMappingBuffer + copyOffset;
    }

    internalBuffers.emplace_back(std::move(buffer));
}