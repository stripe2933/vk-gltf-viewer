module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.Materials;

import std;
export import fastgltf;
export import glm;
import vku;
export import vk_mem_alloc_hpp;
export import vulkan_hpp;
import :helpers.fastgltf;
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
            // This is workaround for Clang 18's bug that ranges::views::concat cannot be used with std::optional<shader_type::Material>.
            // TODO: change it to use ranges::views::concat when available.
            std::vector<shader_type::Material> bufferData;
            bufferData.reserve(asset.materials.size() + useFallbackMaterialAtZero);
            if (useFallbackMaterialAtZero) {
                bufferData.push_back({});
            }
            bufferData.append_range(asset.materials | std::views::transform([&](const fastgltf::Material& material) {
                shader_type::Material result {
                    .metallicFactor = material.pbrData.metallicFactor,
                    .roughnessFactor = material.pbrData.roughnessFactor,
                    .baseColorFactor = glm::gtc::make_vec4(material.pbrData.baseColorFactor.data()),
                    .emissiveFactor = glm::gtc::make_vec3(material.emissiveFactor.data()),
                    .alphaCutOff = material.alphaCutoff,
                };

                if (const auto& textureInfo = material.pbrData.baseColorTexture) {
                    result.baseColorPackedTextureInfo = packTextureInfo(*textureInfo);

                    if (const auto &transform = textureInfo->transform) {
                        result.baseColorTextureTransform = getTextureTransform(*transform);
                    }
                }
                if (const auto& textureInfo = material.pbrData.metallicRoughnessTexture) {
                    result.metallicRoughnessPackedTextureInfo = packTextureInfo(*textureInfo);

                    if (const auto &transform = textureInfo->transform) {
                        result.metallicRoughnessTextureTransform = getTextureTransform(*transform);
                    }
                }
                if (const auto& textureInfo = material.normalTexture) {
                    result.normalPackedTextureInfo = packTextureInfo(*textureInfo);
                    result.normalScale = textureInfo->scale;

                    if (const auto &transform = textureInfo->transform) {
                        result.normalTextureTransform = getTextureTransform(*transform);
                    }
                }
                if (const auto& textureInfo = material.occlusionTexture) {
                    result.occlusionPackedTextureInfo = packTextureInfo(*textureInfo);
                    result.occlusionStrength = textureInfo->strength;

                    if (const auto &transform = textureInfo->transform) {
                        result.occlusionTextureTransform = getTextureTransform(*transform);
                    }
                }
                if (const auto& textureInfo = material.emissiveTexture) {
                    result.emissivePackedTextureInfo = packTextureInfo(*textureInfo);

                    if (const auto &transform = textureInfo->transform) {
                        result.emissiveTextureTransform = getTextureTransform(*transform);
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

        [[nodiscard]] static std::uint32_t packTextureInfo(const fastgltf::TextureInfo &textureInfo) noexcept {
            return (textureInfo.textureIndex + 1) << 2 | getTexcoordIndex(textureInfo);
        }
    };
}