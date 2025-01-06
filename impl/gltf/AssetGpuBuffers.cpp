module;

#include <cassert>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetGpuBuffers;

import std;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.ranges;

[[nodiscard]] std::pair<glm::mat2, glm::vec2> getTextureTransformMatrixPair(const fastgltf::TextureTransform &transform) noexcept {
    const float c = std::cos(transform.rotation), s = std::sin(transform.rotation);
    return {
        { // Note: column major. A row in code actually means a column in the matrix.
            transform.uvScale[0] * c, transform.uvScale[0] * -s,
            transform.uvScale[1] * s, transform.uvScale[1] * c },
        { transform.uvOffset[0], transform.uvOffset[1] },
    };
}

bool vk_gltf_viewer::gltf::AssetGpuBuffers::updatePrimitiveMaterial(
    const fastgltf::Primitive &primitive,
    std::uint32_t materialIndex,
    vk::CommandBuffer transferCommandBuffer
) {
    const std::uint16_t orderedPrimitiveIndex = primitiveInfos.at(&primitive).index;
    const std::uint32_t paddedMaterialIndex = padMaterialIndex(materialIndex);
    return std::visit(multilambda {
        [&](vku::MappedBuffer &primitiveBuffer) {
            primitiveBuffer.asRange<GpuPrimitive>()[orderedPrimitiveIndex].materialIndex = paddedMaterialIndex;
            return false;
        },
        [&](vk::Buffer primitiveBuffer) {
            transferCommandBuffer.updateBuffer(
                primitiveBuffer,
                sizeof(GpuPrimitive) * orderedPrimitiveIndex + offsetof(GpuPrimitive, materialIndex),
                sizeof(GpuPrimitive::materialIndex),
                &paddedMaterialIndex);
            return true;
        }
    }, primitiveBuffer);
}

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

                    if (const auto &transform = baseColorTexture->transform) {
                        std::tie(gpuMaterial.baseColorTextureTransformUpperLeft2x2, gpuMaterial.baseColorTextureTransformOffset)
                            = getTextureTransformMatrixPair(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.baseColorTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& metallicRoughnessTexture = material.pbrData.metallicRoughnessTexture) {
                    gpuMaterial.metallicRoughnessTexcoordIndex = metallicRoughnessTexture->texCoordIndex;
                    gpuMaterial.metallicRoughnessTextureIndex = static_cast<std::int16_t>(metallicRoughnessTexture->textureIndex);

                    if (const auto &transform = metallicRoughnessTexture->transform) {
                        std::tie(gpuMaterial.metallicRoughnessTextureTransformUpperLeft2x2, gpuMaterial.metallicRoughnessTextureTransformOffset)
                            = getTextureTransformMatrixPair(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.metallicRoughnessTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& normalTexture = material.normalTexture) {
                    gpuMaterial.normalTexcoordIndex = normalTexture->texCoordIndex;
                    gpuMaterial.normalTextureIndex = static_cast<std::int16_t>(normalTexture->textureIndex);
                    gpuMaterial.normalScale = normalTexture->scale;

                    if (const auto &transform = normalTexture->transform) {
                        std::tie(gpuMaterial.normalTextureTransformUpperLeft2x2, gpuMaterial.normalTextureTransformOffset)
                            = getTextureTransformMatrixPair(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.normalTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& occlusionTexture = material.occlusionTexture) {
                    gpuMaterial.occlusionTexcoordIndex = occlusionTexture->texCoordIndex;
                    gpuMaterial.occlusionTextureIndex = static_cast<std::int16_t>(occlusionTexture->textureIndex);
                    gpuMaterial.occlusionStrength = occlusionTexture->strength;

                    if (const auto &transform = occlusionTexture->transform) {
                        std::tie(gpuMaterial.occlusionTextureTransformUpperLeft2x2, gpuMaterial.occlusionTextureTransformOffset)
                            = getTextureTransformMatrixPair(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.occlusionTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& emissiveTexture = material.emissiveTexture) {
                    gpuMaterial.emissiveTexcoordIndex = emissiveTexture->texCoordIndex;
                    gpuMaterial.emissiveTextureIndex = static_cast<std::int16_t>(emissiveTexture->textureIndex);

                    if (const auto &transform = emissiveTexture->transform) {
                        std::tie(gpuMaterial.emissiveTextureTransformUpperLeft2x2, gpuMaterial.emissiveTextureTransformOffset)
                            = getTextureTransformMatrixPair(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.emissiveTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
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

std::variant<vku::AllocatedBuffer, vku::MappedBuffer> vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveBuffer() {
    vku::MappedBuffer stagingBuffer {
        gpu.allocator,
        std::from_range, orderedPrimitives | std::views::transform([this](const fastgltf::Primitive *pPrimitive) {
            const AssetPrimitiveInfo &primitiveInfo = primitiveInfos[pPrimitive];

            // If normal and tangent not presented (nullopt), it will use a faceted mesh renderer, and they will does not
            // dereference those buffers. Therefore, it is okay to pass nullptr into shaders
            const auto normalInfo = primitiveInfo.normalInfo.value_or(AssetPrimitiveInfo::AttributeBufferInfo{});
            const auto tangentInfo = primitiveInfo.tangentInfo.value_or(AssetPrimitiveInfo::AttributeBufferInfo{});

            // If color is not presented, it is not used in the shader. Therefore, it is okay to pass nullptr into shaders.
            const auto colorInfo = primitiveInfo.colorInfo.value_or(AssetPrimitiveInfo::ColorAttributeBufferInfo{});

            return GpuPrimitive {
                .pPositionBuffer = primitiveInfo.positionInfo.address,
                .pNormalBuffer = normalInfo.address,
                .pTangentBuffer = tangentInfo.address,
                .pTexcoordAttributeMappingInfoBuffer = primitiveInfo.texcoordsInfo.pMappingBuffer,
                .pColorBuffer = colorInfo.address,
                .positionByteStride = primitiveInfo.positionInfo.byteStride,
                .normalByteStride = normalInfo.byteStride,
                .tangentByteStride = tangentInfo.byteStride,
                .colorByteStride = colorInfo.byteStride,
                .materialIndex = primitiveInfo.materialIndex.transform(padMaterialIndex).value_or(0U),
            };
        }),
        gpu.isUmaDevice ? vk::BufferUsageFlagBits::eStorageBuffer : vk::BufferUsageFlagBits::eTransferSrc,
    };

    if (gpu.isUmaDevice || vku::contains(gpu.allocator.getAllocationMemoryProperties(stagingBuffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
        return stagingBuffer;
    }

    vku::AllocatedBuffer dstBuffer{ gpu.allocator, vk::BufferCreateInfo {
        {},
        stagingBuffer.size,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    } };
    stagingInfos.emplace_back(
        std::move(stagingBuffer).unmap(),
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