module;

#include <cassert>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetGpuBuffers;

import std;
import :helpers.concepts;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.ranges;
import :vulkan.buffer;

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
    vku::AllocatedBuffer buffer = vku::MappedBuffer {
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
    stageIfNeeded(buffer, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);

    return buffer;
}

std::variant<vku::AllocatedBuffer, vku::MappedBuffer> vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveBuffer() {
    vku::AllocatedBuffer buffer = vku::MappedBuffer {
        gpu.allocator,
        std::from_range, orderedPrimitives | std::views::transform([this](const fastgltf::Primitive *pPrimitive) {
            const AssetPrimitiveInfo &primitiveInfo = primitiveInfos[pPrimitive];
            GpuPrimitive gpuPrimitive {
                .pPositionBuffer = primitiveInfo.positionInfo.address,
                .pPositionMorphTargetAttributeMappingInfoBuffer = primitiveInfo.positionMorphTargetInfos.pMappingBuffer,
                .pTexcoordAttributeMappingInfoBuffer = primitiveInfo.texcoordsInfo.pMappingBuffer,
                .positionByteStride = primitiveInfo.positionInfo.byteStride,
                .materialIndex = primitiveInfo.materialIndex.transform(padMaterialIndex).value_or(0U),
            };

            if (primitiveInfo.normalInfo) {
                gpuPrimitive.pNormalBuffer = primitiveInfo.normalInfo->address;
                gpuPrimitive.normalByteStride = primitiveInfo.normalInfo->byteStride;
                gpuPrimitive.pNormalMorphTargetAttributeMappingInfoBuffer
                    = primitiveInfo.normalMorphTargetInfos.pMappingBuffer;
            }
            if (primitiveInfo.tangentInfo) {
                gpuPrimitive.pTangentBuffer = primitiveInfo.tangentInfo->address;
                gpuPrimitive.tangentByteStride = primitiveInfo.tangentInfo->byteStride;
                gpuPrimitive.pTangentMorphTargetAttributeMappingInfoBuffer
                    = primitiveInfo.tangentMorphTargetInfos.pMappingBuffer;
            }
            if (primitiveInfo.colorInfo) {
                gpuPrimitive.pColorBuffer = primitiveInfo.colorInfo->address;
                gpuPrimitive.colorByteStride = primitiveInfo.colorInfo->byteStride;
                gpuPrimitive.colorComponentType = primitiveInfo.colorInfo->componentType;
                gpuPrimitive.colorComponentCount = primitiveInfo.colorInfo->componentCount;
            }

            return gpuPrimitive;
        }),
        gpu.isUmaDevice ? vk::BufferUsageFlagBits::eStorageBuffer : vk::BufferUsageFlagBits::eTransferSrc,
    }.unmap();
    stageIfNeeded(buffer, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
    return buffer;
}

void vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveIndexedAttributeMappingBuffers() {
    // Collect primitives that have any TEXCOORD attributes.
    const std::vector primitiveWithTexcoords
        = primitiveInfos
        | std::views::values
        | std::views::filter([](const AssetPrimitiveInfo &primitiveInfo) {
            return !primitiveInfo.texcoordsInfo.attributeInfos.empty();
        })
        | ranges::views::addressof
        | std::ranges::to<std::vector>();
    if (primitiveWithTexcoords.empty()) {
        return;
    }

    auto [buffer, copyOffsets] = vulkan::buffer::createCombinedBuffer<true>(
        gpu.allocator,
        primitiveWithTexcoords | std::views::transform([](const AssetPrimitiveInfo *pPrimitiveInfo) {
            return std::span { pPrimitiveInfo->texcoordsInfo.attributeInfos };
        }),
        gpu.isUmaDevice
            ? vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
            : vk::BufferUsageFlagBits::eTransferSrc);
    stageIfNeeded(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

    const vk::DeviceAddress pIndexAttributeMappingBuffer = gpu.device.getBufferAddress({ buffer });
    for (auto [pPrimitiveInfo, copyOffset] : std::views::zip(primitiveWithTexcoords, copyOffsets)) {
        pPrimitiveInfo->texcoordsInfo.pMappingBuffer = pIndexAttributeMappingBuffer + copyOffset;
    }

    internalBuffers.emplace_back(std::move(buffer));
}

void vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveMorphTargetIndexedAttributeMappingBuffers() {
    const auto processMorphTargetPrimitives = [this](
        std::span<AssetPrimitiveInfo* const> morphTargetPrimitives,
        concepts::signature_of<AssetPrimitiveInfo::IndexedAttributeBufferInfos&, AssetPrimitiveInfo&> auto const &indexedAttributeBufferInfoGetter
    ) -> vku::AllocatedBuffer {
        auto [buffer, copyOffsets] = vulkan::buffer::createCombinedBuffer<true>(
            gpu.allocator,
            morphTargetPrimitives | std::views::transform([&](AssetPrimitiveInfo *pPrimitiveInfo) {
                return std::span { indexedAttributeBufferInfoGetter(*pPrimitiveInfo).attributeInfos };
            }),
            gpu.isUmaDevice
                ? vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
                : vk::BufferUsageFlagBits::eTransferSrc);
        stageIfNeeded(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

        const vk::DeviceAddress pIndexAttributeMappingBuffer = gpu.device.getBufferAddress({ buffer });
        for (auto [pPrimitiveInfo, copyOffset] : std::views::zip(morphTargetPrimitives, copyOffsets)) {
            indexedAttributeBufferInfoGetter(*pPrimitiveInfo).pMappingBuffer = pIndexAttributeMappingBuffer + copyOffset;
        }

        return std::move(buffer);
    };

    // Position morph targets.
    std::vector morphTargetPrimitives
        = primitiveInfos
        | std::views::values
        | std::views::filter([](const AssetPrimitiveInfo &primitiveInfo) {
            return !primitiveInfo.positionMorphTargetInfos.attributeInfos.empty();
        })
        | ranges::views::addressof
        | std::ranges::to<std::vector>();
    if (!morphTargetPrimitives.empty()) {
        internalBuffers.emplace_back(
            processMorphTargetPrimitives(
                morphTargetPrimitives,
                [](AssetPrimitiveInfo &x) -> AssetPrimitiveInfo::IndexedAttributeBufferInfos& {
                    return x.positionMorphTargetInfos;
                }));
    }

    // Normal morph targets.
    morphTargetPrimitives.clear();
    morphTargetPrimitives.append_range(
        primitiveInfos
            | std::views::values
            | std::views::filter([](const AssetPrimitiveInfo &primitiveInfo) {
                return !primitiveInfo.normalMorphTargetInfos.attributeInfos.empty();
            })
            | ranges::views::addressof);
    if (!morphTargetPrimitives.empty()) {
        internalBuffers.emplace_back(
            processMorphTargetPrimitives(
                morphTargetPrimitives,
                [](AssetPrimitiveInfo &x) -> AssetPrimitiveInfo::IndexedAttributeBufferInfos& {
                    return x.normalMorphTargetInfos;
                }));
    }

    // Tangent morph targets.
    morphTargetPrimitives.clear();
    morphTargetPrimitives.append_range(
        primitiveInfos
            | std::views::values
            | std::views::filter([](const AssetPrimitiveInfo &primitiveInfo) {
                return !primitiveInfo.tangentMorphTargetInfos.attributeInfos.empty();
            })
            | ranges::views::addressof);
    if (!morphTargetPrimitives.empty()) {
        internalBuffers.emplace_back(
            processMorphTargetPrimitives(
                morphTargetPrimitives,
                [](AssetPrimitiveInfo &x) -> AssetPrimitiveInfo::IndexedAttributeBufferInfos& {
                    return x.tangentMorphTargetInfos;
                }));
    }
}
