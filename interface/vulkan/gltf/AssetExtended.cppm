module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.gltf.AssetExtended;

import std;
export import BS.thread_pool;
export import vkgltf.bindless;

export import vk_gltf_viewer.gltf.AssetExtended;
import vk_gltf_viewer.helpers.fastgltf;
export import vk_gltf_viewer.vulkan.buffer.Materials;
export import vk_gltf_viewer.vulkan.buffer.PrimitiveAttributes;
export import vk_gltf_viewer.vulkan.texture.Textures;
export import vk_gltf_viewer.vulkan.texture.ImGuiColorSpaceAndUsageCorrectedTextures;

namespace vk_gltf_viewer::vulkan::gltf {
    export class AssetExtended : public vk_gltf_viewer::gltf::AssetExtended {
    public:
        buffer::Materials materialBuffer;
        vkgltf::CombinedIndexBuffer combinedIndexBuffer;
        std::unordered_map<const fastgltf::Primitive*, vkgltf::PrimitiveAttributeBuffers> primitiveAttributeBuffers;
        vkgltf::PrimitiveBuffer primitiveBuffer;
        std::optional<vkgltf::SkinBuffer> skinBuffer;
        texture::Textures textures;
        texture::ImGuiColorSpaceAndUsageCorrectedTextures imGuiColorSpaceAndUsageCorrectedTextures;

        AssetExtended(
            const std::filesystem::path &path,
            const Gpu &gpu LIFETIMEBOUND,
            const texture::Fallback &fallbackTexture LIFETIMEBOUND,
            vkgltf::StagingBufferStorage &stagingBufferStorage,
            BS::thread_pool<> threadPool = {}
        );

        [[nodiscard]] ImTextureID getEmissiveTextureID(std::size_t materialIndex) const override;
        [[nodiscard]] ImTextureID getMetallicTextureID(std::size_t materialIndex) const override;
        [[nodiscard]] ImTextureID getNormalTextureID(std::size_t materialIndex) const override;
        [[nodiscard]] ImTextureID getOcclusionTextureID(std::size_t materialIndex) const override;
        [[nodiscard]] ImTextureID getRoughnessTextureID(std::size_t materialIndex) const override;
        [[nodiscard]] ImTextureID getTextureID(std::size_t textureIndex) const override;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::gltf::AssetExtended::AssetExtended(
    const std::filesystem::path &path,
    const Gpu &gpu,
    const texture::Fallback &fallbackTexture,
    vkgltf::StagingBufferStorage &stagingBufferStorage,
    BS::thread_pool<> threadPool
) : vk_gltf_viewer::gltf::AssetExtended { path },
    materialBuffer { asset, gpu.allocator, stagingBufferStorage },
    combinedIndexBuffer { asset, gpu.allocator, vkgltf::CombinedIndexBuffer::Config {
        .adapter = externalBuffers,
        .promoteUnsignedByteToUnsignedShort = !gpu.supportUint8Index,
        .usageFlags = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferSrc,
        .queueFamilies = gpu.queueFamilies.uniqueIndices,
        .stagingInfo = vku::unsafeAddress(vkgltf::StagingInfo { stagingBufferStorage }),
    } },
    primitiveAttributeBuffers { buffer::createPrimitiveAttributeBuffers(*this, gpu, stagingBufferStorage, threadPool) },
    primitiveBuffer { asset, primitiveAttributeBuffers, gpu.device, gpu.allocator, vkgltf::PrimitiveBuffer::Config {
        .materialIndexFn = [](const fastgltf::Primitive &primitive) noexcept -> std::int32_t {
            // First element of the material storage buffer is reserved for the fallback material.
            if (!primitive.materialIndex) return 0;
            return 1 + *primitive.materialIndex;
        },
        .usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
        .queueFamilies = gpu.queueFamilies.uniqueIndices,
        .stagingInfo = vku::unsafeAddress(vkgltf::StagingInfo { stagingBufferStorage }),
    } },
    skinBuffer { vkgltf::SkinBuffer::from(asset, gpu.allocator, vkgltf::SkinBuffer::Config {
        .adapter = externalBuffers,
        .usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
        .queueFamilies = gpu.queueFamilies.uniqueIndices,
        .stagingInfo = vku::unsafeAddress(vkgltf::StagingInfo { stagingBufferStorage }),
    }) },
    textures { *this, gpu, fallbackTexture, threadPool },
    imGuiColorSpaceAndUsageCorrectedTextures { asset, textures, gpu } { }

ImTextureID vk_gltf_viewer::vulkan::gltf::AssetExtended::getEmissiveTextureID(std::size_t materialIndex) const {
    return imGuiColorSpaceAndUsageCorrectedTextures.getEmissiveTextureID(materialIndex);
}

ImTextureID vk_gltf_viewer::vulkan::gltf::AssetExtended::getMetallicTextureID(std::size_t materialIndex) const {
    return imGuiColorSpaceAndUsageCorrectedTextures.getMetallicTextureID(materialIndex);
}

ImTextureID vk_gltf_viewer::vulkan::gltf::AssetExtended::getNormalTextureID(std::size_t materialIndex) const {
    return imGuiColorSpaceAndUsageCorrectedTextures.getNormalTextureID(materialIndex);
}

ImTextureID vk_gltf_viewer::vulkan::gltf::AssetExtended::getOcclusionTextureID(std::size_t materialIndex) const {
    return imGuiColorSpaceAndUsageCorrectedTextures.getOcclusionTextureID(materialIndex);
}

ImTextureID vk_gltf_viewer::vulkan::gltf::AssetExtended::getRoughnessTextureID(std::size_t materialIndex) const {
    return imGuiColorSpaceAndUsageCorrectedTextures.getRoughnessTextureID(materialIndex);
}

ImTextureID vk_gltf_viewer::vulkan::gltf::AssetExtended::getTextureID(std::size_t textureIndex) const {
    return imGuiColorSpaceAndUsageCorrectedTextures.getTextureID(textureIndex);
}
