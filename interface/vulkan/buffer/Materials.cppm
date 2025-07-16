module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer.vulkan.buffer.Materials;

import std;
export import fastgltf;
export import vkgltf; // vkgltf::StagingBufferStorage
export import vku;

export import vk_gltf_viewer.vulkan.shader_type.Material;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Materials final : public vku::AllocatedBuffer {
    public:
        vk::DescriptorBufferInfo descriptorInfo;

        Materials(const fastgltf::Asset &asset, vma::Allocator allocator, vkgltf::StagingBufferStorage &stagingBufferStorage);

        /**
         * @brief Update material property with field accessor.
         *
         * @tparam accessor Member accessor of <tt>shader_type::Material</tt> to update.
         * @param materialIndex Index of asset material to update.
         * @param data Data to update, must be the type of accessor function's return type.
         * @param transferCommandBuffer If buffer is not host-visible memory and so is unable to be updated from the host, this command buffer will be used for recording the buffer update command. Then, its execution MUST be synchronized to be available to the buffer usage. Otherwise, this parameter is unused.
         * @return <tt>true</tt> if the buffer is not host-visible memory and the update command is recorded, <tt>false</tt> otherwise.
         * @note <tt>materialIndex = 0</tt> will refer <tt>asset.materials[0]</tt>, NOT fallback material.
         */
        template <auto shader_type::Material::*accessor>
        bool update(
            std::size_t materialIndex,
            const std::remove_cvref_t<std::invoke_result_t<decltype(accessor), shader_type::Material&>>& data,
            vk::CommandBuffer transferCommandBuffer
        ) {
            // Obtain byte offset and size of the field to be updated.
            static constexpr shader_type::Material dummy{};
            constexpr auto fieldAddress = &std::invoke(accessor, dummy);
            const vk::DeviceSize byteOffset
                = sizeof(shader_type::Material) * (1 + materialIndex)
                + reinterpret_cast<std::uintptr_t>(fieldAddress) - reinterpret_cast<std::uintptr_t>(&dummy);
            constexpr vk::DeviceSize byteSize = sizeof(data);
            static_assert(byteSize % 4 == 0 && "Data size bytes must be multiple of 4.");

            if (vku::contains(allocator.getAllocationMemoryProperties(allocation), vk::MemoryPropertyFlagBits::eHostVisible)) {
                allocator.copyMemoryToAllocation(&data, allocation, byteOffset, byteSize);
                return false;
            }
            else {
                transferCommandBuffer.updateBuffer(*this, byteOffset, byteSize, &data);
                return true;
            }
        }
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

[[nodiscard]] glm::mat3x2 getTextureTransform(const fastgltf::TextureTransform &transform) noexcept {
    const float c = std::cos(transform.rotation), s = std::sin(transform.rotation);
    return { // Note: column major. A row in code actually means a column in the matrix.
        transform.uvScale[0] * c, transform.uvScale[0] * -s,
        transform.uvScale[1] * s, transform.uvScale[1] * c,
        transform.uvOffset[0], transform.uvOffset[1],
    };
}

vk_gltf_viewer::vulkan::buffer::Materials::Materials(
    const fastgltf::Asset &asset,
    vma::Allocator allocator,
    vkgltf::StagingBufferStorage &stagingBufferStorage
) : AllocatedBuffer {
        allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(shader_type::Material) * (1 + asset.materials.size()), // +1 for fallback material.
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferHost,
        },
    },
    descriptorInfo { *this, 0, vk::WholeSize } {
    auto it = std::span { static_cast<shader_type::Material*>(allocator.getAllocationInfo(allocation).pMappedData), 1 + asset.materials.size() }.begin();
    *it++ = {}; // Initialize fallback material.
    std::ranges::transform(asset.materials, it, [](const fastgltf::Material &material) {
        shader_type::Material result {
            .baseColorFactor = glm::gtc::make_vec4(material.pbrData.baseColorFactor.data()),
            .metallicFactor = material.pbrData.metallicFactor,
            .roughnessFactor = material.pbrData.roughnessFactor,
            .emissive = material.emissiveStrength * glm::gtc::make_vec3(material.emissiveFactor.data()),
            .alphaCutOff = material.alphaCutoff,
            .ior = material.ior,
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
    });

    if (!vku::contains(allocator.getAllocationMemoryProperties(allocation), vk::MemoryPropertyFlagBits::eHostCoherent)) {
        // Created buffer is non-coherent. Flush the mapped memory range.
        allocator.flushAllocation(allocation, 0, size);
    }

    if (stagingBufferStorage.stage(*this, vk::BufferUsageFlagBits::eStorageBuffer)) {
        descriptorInfo.buffer = *this;
    }
}