module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.Materials;

import std;
export import fastgltf;
export import glm;
import vku;
export import vk_mem_alloc_hpp;
export import vulkan_hpp;
import :helpers.optional;
import :helpers.ranges;
export import :vulkan.buffer.StagingBufferStorage;
import :vulkan.trait.PostTransferObject;

[[nodiscard]] glm::mat3x2 getTextureTransform(const fastgltf::TextureTransform &transform) noexcept {
    const float c = std::cos(transform.rotation), s = std::sin(transform.rotation);
    return { // Note: column major. A row in code actually means a column in the matrix.
        transform.uvScale[0] * c, transform.uvScale[0] * -s,
        transform.uvScale[1] * s, transform.uvScale[1] * c,
        transform.uvOffset[0], transform.uvOffset[1],
    };
}

namespace vk_gltf_viewer::vulkan::buffer {
    export class Materials : trait::PostTransferObject {
    public:
        struct GpuMaterial {
            std::uint8_t baseColorTexcoordIndex;
            std::uint8_t metallicRoughnessTexcoordIndex;
            std::uint8_t normalTexcoordIndex;
            std::uint8_t occlusionTexcoordIndex;
            std::uint8_t emissiveTexcoordIndex;
            char padding0[1];
            std::uint16_t baseColorTextureIndex = 0;
            std::uint16_t metallicRoughnessTextureIndex = 0;
            std::uint16_t normalTextureIndex = 0;
            std::uint16_t occlusionTextureIndex = 0;
            std::uint16_t emissiveTextureIndex = 0;
            glm::vec4 baseColorFactor = { 1.f, 1.f, 1.f, 1.f };
            float metallicFactor = 1.f;
            float roughnessFactor = 1.f;
            float normalScale = 1.f;
            float occlusionStrength = 1.f;
            glm::vec3 emissiveFactor = { 0.f, 0.f, 0.f };
            float alphaCutOff;
            glm::mat3x2 baseColorTextureTransform;
            glm::mat3x2 metallicRoughnessTextureTransform;
            glm::mat3x2 normalTextureTransform;
            glm::mat3x2 occlusionTextureTransform;
            glm::mat3x2 emissiveTextureTransform;
            char padding1[8];
        };
        static_assert(sizeof(GpuMaterial) == 192);

        Materials(
            const fastgltf::Asset &asset,
            vma::Allocator allocator,
            StagingBufferStorage &stagingBufferStorage
        ) : PostTransferObject { stagingBufferStorage },
            useFallbackMaterialAtZero { determineUseFallbackMaterialAtZero(asset) },
            buffer { createBuffer(asset, allocator) },
            descriptorInfo { buffer, 0, vk::WholeSize } { }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        [[nodiscard]] std::uint32_t padMaterialIndex(std::uint32_t materialIndex) const noexcept {
            return materialIndex + useFallbackMaterialAtZero;
        }

    private:
        bool useFallbackMaterialAtZero;
        vku::AllocatedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;

        [[nodiscard]] static bool determineUseFallbackMaterialAtZero(const fastgltf::Asset &asset) noexcept {
            // If any primitive of the asset is missing the material, we have use the fallback material.
            const auto primitives = asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join;
            for (const fastgltf::Primitive &primitive : primitives) {
                if (!primitive.materialIndex.has_value()) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] vku::AllocatedBuffer createBuffer(const fastgltf::Asset &asset, vma::Allocator allocator) const {
            // This is workaround for Clang 18's bug that ranges::views::concat cannot be used with std::optional<GpuMaterial>.
            // TODO: change it to use ranges::views::concat when available.
            std::vector<GpuMaterial> bufferData;
            bufferData.reserve(asset.materials.size() + useFallbackMaterialAtZero);
            if (useFallbackMaterialAtZero) {
                bufferData.push_back({});
            }
            bufferData.append_range(asset.materials | std::views::transform([&](const fastgltf::Material& material) {
                GpuMaterial gpuMaterial {
                    .baseColorFactor = glm::gtc::make_vec4(material.pbrData.baseColorFactor.data()),
                    .metallicFactor = material.pbrData.metallicFactor,
                    .roughnessFactor = material.pbrData.roughnessFactor,
                    .emissiveFactor = glm::gtc::make_vec3(material.emissiveFactor.data()),
                    .alphaCutOff = material.alphaCutoff,
                };

                if (const auto& baseColorTexture = material.pbrData.baseColorTexture) {
                    gpuMaterial.baseColorTexcoordIndex = baseColorTexture->texCoordIndex;
                    gpuMaterial.baseColorTextureIndex = static_cast<std::uint16_t>(baseColorTexture->textureIndex) + 1;

                    if (const auto &transform = baseColorTexture->transform) {
                        gpuMaterial.baseColorTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.baseColorTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& metallicRoughnessTexture = material.pbrData.metallicRoughnessTexture) {
                    gpuMaterial.metallicRoughnessTexcoordIndex = metallicRoughnessTexture->texCoordIndex;
                    gpuMaterial.metallicRoughnessTextureIndex = static_cast<std::uint16_t>(metallicRoughnessTexture->textureIndex) + 1;

                    if (const auto &transform = metallicRoughnessTexture->transform) {
                        gpuMaterial.metallicRoughnessTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.metallicRoughnessTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& normalTexture = material.normalTexture) {
                    gpuMaterial.normalTexcoordIndex = normalTexture->texCoordIndex;
                    gpuMaterial.normalTextureIndex = static_cast<std::uint16_t>(normalTexture->textureIndex) + 1;
                    gpuMaterial.normalScale = normalTexture->scale;

                    if (const auto &transform = normalTexture->transform) {
                        gpuMaterial.normalTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.normalTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& occlusionTexture = material.occlusionTexture) {
                    gpuMaterial.occlusionTexcoordIndex = occlusionTexture->texCoordIndex;
                    gpuMaterial.occlusionTextureIndex = static_cast<std::uint16_t>(occlusionTexture->textureIndex) + 1;
                    gpuMaterial.occlusionStrength = occlusionTexture->strength;

                    if (const auto &transform = occlusionTexture->transform) {
                        gpuMaterial.occlusionTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.occlusionTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& emissiveTexture = material.emissiveTexture) {
                    gpuMaterial.emissiveTexcoordIndex = emissiveTexture->texCoordIndex;
                    gpuMaterial.emissiveTextureIndex = static_cast<std::uint16_t>(emissiveTexture->textureIndex) + 1;

                    if (const auto &transform = emissiveTexture->transform) {
                        gpuMaterial.emissiveTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            gpuMaterial.emissiveTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }

                return gpuMaterial;
            }));
            
            vku::AllocatedBuffer buffer = vku::MappedBuffer {
                allocator,
                std::from_range, bufferData,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            }.unmap();
            if (StagingBufferStorage::needStaging(buffer)) {
                stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer);
            }

            return buffer;
        }
    };
}