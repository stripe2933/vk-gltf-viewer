module;

#include <cassert>

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
         * @brief Replace self with a new buffer with doubled size.
         *
         * The original data is preserved by:
         * - If the old buffer data can be copied to the new buffer in host, data is copied and <tt>std::nullopt</tt> returned.
         * - Otherwise, \p transferCommandBuffer is recorded and the old buffer is returned. You must retain the lifetime
         *   of the returned buffer until the command buffer is executed.
         *
         * @param transferCommandBuffer If buffer is not host-visible memory and so is unable to be updated from the host, this command buffer will be used for recording the buffer update command. Then, its execution MUST be synchronized to be available to the buffer usage. Otherwise, this parameter is unused.
         * @return <tt>std::nullopt</tt> if the buffer is host-visible memory and the data is copied, or an <tt>AllocatedBuffer</tt> with the old buffer data recorded in \p transferCommandBuffer.
         */
        [[nodiscard]] std::optional<AllocatedBuffer> enlarge(vk::CommandBuffer transferCommandBuffer);

        /**
         * @brief Check if a material can be added at the end of the buffer.
         * @return <tt>true</tt> if the buffer has enough space to store a new material, <tt>false</tt> otherwise.
         */
        [[nodiscard]] bool canAddMaterial() const noexcept;

        /**
         * Add new material at the end of the buffer.
         *
         * Buffer must have enough space to store the new material. You can use <tt>canAddMaterial()</tt> to check
         * if the buffer can store the new material. If the method returns <tt>false</tt>, use
         * <tt>enlarge(vk::CommandBuffer)</tt> to enlarge the buffer by 2x capacity.
         *
         * @param material Material to add.
         * @param transferCommandBuffer If buffer is not host-visible memory and so is unable to be updated from the host, this command buffer will be used for recording the buffer update command. Then, its execution MUST be synchronized to be available to the buffer usage. Otherwise, this parameter is unused.
         * @return <tt>true</tt> if the buffer is not host-visible memory and the update command is recorded, <tt>false</tt> otherwise.
         */
        [[nodiscard]] bool add(const fastgltf::Material &material, vk::CommandBuffer transferCommandBuffer);

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

    private:
        /**
         * @brief Count of the currently stored materials, including the fallback material.
         */
        std::size_t count;
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

[[nodiscard]] vk_gltf_viewer::vulkan::shader_type::Material getShaderMaterial(const fastgltf::Material &material) {
    vk_gltf_viewer::vulkan::shader_type::Material result {
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
            vk::BufferUsageFlagBits::eStorageBuffer
                | vk::BufferUsageFlagBits::eTransferSrc /* might be copy source when enlarging the buffer */,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferHost,
        },
    },
    descriptorInfo { *this, 0, vk::WholeSize },
    count { 1 + asset.materials.size() } {
    auto it = std::span { static_cast<shader_type::Material*>(allocator.getAllocationInfo(allocation).pMappedData), 1 + asset.materials.size() }.begin();
    *it++ = {}; // Initialize fallback material.
    std::ranges::transform(asset.materials, it, getShaderMaterial);

    if (!vku::contains(allocator.getAllocationMemoryProperties(allocation), vk::MemoryPropertyFlagBits::eHostCoherent)) {
        // Created buffer is non-coherent. Flush the mapped memory range.
        allocator.flushAllocation(allocation, 0, size);
    }

    if (stagingBufferStorage.stage(*this, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc /* might be copy source when enlarging the buffer */)) {
        descriptorInfo.buffer = *this;
    }
}

std::optional<vku::AllocatedBuffer> vk_gltf_viewer::vulkan::buffer::Materials::enlarge(vk::CommandBuffer transferCommandBuffer) {
    // Create new buffer with doubled size.
    AllocatedBuffer newBuffer {
        allocator,
        vk::BufferCreateInfo {
            {},
            size * 2,
            vk::BufferUsageFlagBits::eTransferDst /* copy destination */
                | vk::BufferUsageFlagBits::eStorageBuffer
                | vk::BufferUsageFlagBits::eTransferSrc /* might be copy source when enlarging the buffer */,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        },
    };

    descriptorInfo.buffer = newBuffer;

    if (vku::contains(allocator.getAllocationMemoryProperties(allocation), vk::MemoryPropertyFlagBits::eHostCached) &&
        vku::contains(allocator.getAllocationMemoryProperties(newBuffer.allocation), vk::MemoryPropertyFlagBits::eHostVisible)) {
        // Old buffer data can be copied to the new buffer in host.
        allocator.copyMemoryToAllocation(allocator.getAllocationInfo(allocation).pMappedData, newBuffer.allocation, 0, size);

        static_cast<AllocatedBuffer&>(*this) = std::move(newBuffer);
        return std::nullopt;
    }
    else {
        // Copy needed to be done in GPU.
        transferCommandBuffer.copyBuffer(*this, newBuffer, vk::BufferCopy { 0, 0, size });
        return std::exchange(static_cast<AllocatedBuffer&>(*this), std::move(newBuffer));
    }
}

bool vk_gltf_viewer::vulkan::buffer::Materials::canAddMaterial() const noexcept {
    return sizeof(shader_type::Material) * (count + 1) <= size;
}

bool vk_gltf_viewer::vulkan::buffer::Materials::add(const fastgltf::Material &material, vk::CommandBuffer transferCommandBuffer) {
    assert(canAddMaterial() && "Buffer size is not enough to push back a new material.");

    const shader_type::Material shaderMaterial = getShaderMaterial(material);
    if (vku::contains(allocator.getAllocationMemoryProperties(allocation), vk::MemoryPropertyFlagBits::eHostVisible)) {
        allocator.copyMemoryToAllocation(&shaderMaterial, allocation, sizeof(shader_type::Material) * count++, sizeof(shader_type::Material));
        return false;
    }
    else {
        transferCommandBuffer.updateBuffer(*this, sizeof(shader_type::Material) * count++, sizeof(shader_type::Material), &shaderMaterial);
        return true;
    }
}
