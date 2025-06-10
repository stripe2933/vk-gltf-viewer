module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer.vulkan.buffer.Materials;

import std;
export import fastgltf;
import vku;
export import vk_mem_alloc_hpp;
export import vulkan_hpp;

import vk_gltf_viewer.helpers.functional;
export import vk_gltf_viewer.vulkan.buffer.StagingBufferStorage;
export import vk_gltf_viewer.vulkan.shader_type.Material;
import vk_gltf_viewer.vulkan.trait.PostTransferObject;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Materials : trait::PostTransferObject {
    public:
        Materials(const fastgltf::Asset &asset, vma::Allocator allocator, StagingBufferStorage &stagingBufferStorage);

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept;

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
            // TODO: currently only data size 4-byte multiple checking is done, since there is no way to obtain the offset
            //  of the member variable from accessor pointer. It could be checked at the runtime, but not implemented
            //  as it is the validation layer's role. Check it at compile time if possible.
            static_assert(sizeof(data) % 4 == 0 && "Data size bytes must be multiple of 4.");

            return std::visit(multilambda {
                [&](vku::MappedBuffer &primitiveBuffer) {
                    std::invoke(accessor, primitiveBuffer.asRange<shader_type::Material>()[materialIndex + 1]) = data;
                    return false;
                },
                [&](vk::Buffer primitiveBuffer) {
                    static constexpr shader_type::Material dummy{};
                    constexpr auto fieldAddress = &std::invoke(accessor, dummy);

                    const vk::DeviceSize fieldOffset = reinterpret_cast<std::uintptr_t>(fieldAddress) - reinterpret_cast<std::uintptr_t>(&dummy);
                    // assert(fieldOffset % 4 == 0 && "Field offset must be 4-byte aligned.");
                    transferCommandBuffer.updateBuffer(primitiveBuffer, sizeof(shader_type::Material) * (materialIndex + 1) + fieldOffset, sizeof(data), &data);
                    return true;
                }
            }, buffer);
        }

    private:
        std::variant<vku::AllocatedBuffer, vku::MappedBuffer> buffer;
        vk::DescriptorBufferInfo descriptorInfo;

        [[nodiscard]] std::variant<vku::AllocatedBuffer, vku::MappedBuffer> createBuffer(const fastgltf::Asset &asset, vma::Allocator allocator) const;
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
    StagingBufferStorage &stagingBufferStorage
) : PostTransferObject { stagingBufferStorage },
    buffer { createBuffer(asset, allocator) },
    descriptorInfo { visit_as<vk::Buffer>(buffer), 0, vk::WholeSize } { }

const vk::DescriptorBufferInfo &vk_gltf_viewer::vulkan::buffer::Materials::getDescriptorInfo() const noexcept {
    return descriptorInfo;
}

std::variant<vku::AllocatedBuffer, vku::MappedBuffer> vk_gltf_viewer::vulkan::buffer::Materials::createBuffer(const fastgltf::Asset &asset, vma::Allocator allocator) const {
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
    }));

    vku::MappedBuffer buffer {
        allocator,
        std::from_range, bufferData,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
    };

    // If staging doesn't have to be done, preserve the mapped state.
    if (!StagingBufferStorage::needStaging(buffer)) {
        return std::variant<vku::AllocatedBuffer, vku::MappedBuffer> { std::in_place_type<vku::MappedBuffer>, std::move(buffer) };
    }

    vku::AllocatedBuffer unmappedBuffer = std::move(buffer).unmap();
    stagingBufferStorage.get().stage(unmappedBuffer, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
    return unmappedBuffer;
}