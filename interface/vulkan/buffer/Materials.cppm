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
import :vulkan.shader_type.Material;
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
        Materials(
            const fastgltf::Asset &asset,
            vma::Allocator allocator,
            StagingBufferStorage &stagingBufferStorage
        ) : PostTransferObject { stagingBufferStorage },
            buffer { createBuffer(asset, allocator) },
            descriptorInfo { buffer, 0, vk::WholeSize } { }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

    private:
        vku::AllocatedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;

        [[nodiscard]] vku::AllocatedBuffer createBuffer(const fastgltf::Asset &asset, vma::Allocator allocator) const {
            // This is workaround for Clang 18's bug that ranges::views::concat cannot be used with std::optional<shader_type::Material>.
            // TODO: change it to use ranges::views::concat when available.
            std::vector<shader_type::Material> bufferData;
            bufferData.reserve(1 + asset.materials.size());
            bufferData.push_back({});
            bufferData.append_range(asset.materials | std::views::transform([&](const fastgltf::Material& material) {
                shader_type::Material result {
                    .baseColorFactor = glm::gtc::make_vec4(material.pbrData.baseColorFactor.data()),
                    .metallicFactor = material.pbrData.metallicFactor,
                    .roughnessFactor = material.pbrData.roughnessFactor,
                    .emissiveFactor = glm::gtc::make_vec3(material.emissiveFactor.data()),
                    .alphaCutOff = material.alphaCutoff,
                };

                if (const auto& baseColorTexture = material.pbrData.baseColorTexture) {
                    result.baseColorTexcoordIndex = baseColorTexture->texCoordIndex;
                    result.baseColorTextureIndex = static_cast<std::uint16_t>(baseColorTexture->textureIndex) + 1;

                    if (const auto &transform = baseColorTexture->transform) {
                        result.baseColorTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            result.baseColorTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& metallicRoughnessTexture = material.pbrData.metallicRoughnessTexture) {
                    result.metallicRoughnessTexcoordIndex = metallicRoughnessTexture->texCoordIndex;
                    result.metallicRoughnessTextureIndex = static_cast<std::uint16_t>(metallicRoughnessTexture->textureIndex) + 1;

                    if (const auto &transform = metallicRoughnessTexture->transform) {
                        result.metallicRoughnessTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            result.metallicRoughnessTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& normalTexture = material.normalTexture) {
                    result.normalTexcoordIndex = normalTexture->texCoordIndex;
                    result.normalTextureIndex = static_cast<std::uint16_t>(normalTexture->textureIndex) + 1;
                    result.normalScale = normalTexture->scale;

                    if (const auto &transform = normalTexture->transform) {
                        result.normalTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            result.normalTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& occlusionTexture = material.occlusionTexture) {
                    result.occlusionTexcoordIndex = occlusionTexture->texCoordIndex;
                    result.occlusionTextureIndex = static_cast<std::uint16_t>(occlusionTexture->textureIndex) + 1;
                    result.occlusionStrength = occlusionTexture->strength;

                    if (const auto &transform = occlusionTexture->transform) {
                        result.occlusionTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            result.occlusionTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }
                if (const auto& emissiveTexture = material.emissiveTexture) {
                    result.emissiveTexcoordIndex = emissiveTexture->texCoordIndex;
                    result.emissiveTextureIndex = static_cast<std::uint16_t>(emissiveTexture->textureIndex) + 1;

                    if (const auto &transform = emissiveTexture->transform) {
                        result.emissiveTextureTransform = getTextureTransform(*transform);
                        if (transform->texCoordIndex) {
                            result.emissiveTexcoordIndex = *transform->texCoordIndex;
                        }
                    }
                }

                return result;
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