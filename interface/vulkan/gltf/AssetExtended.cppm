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
export import vk_gltf_viewer.vulkan.pipeline.PrepassPipelineConfig;
export import vk_gltf_viewer.vulkan.pipeline.PrimitiveRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.UnlitPrimitiveRenderPipeline;
export import vk_gltf_viewer.vulkan.texture.Textures;
export import vk_gltf_viewer.vulkan.texture.ImGuiColorSpaceAndUsageCorrectedTextures;

vk::PrimitiveTopology getListPrimitiveTopology(fastgltf::PrimitiveType type) noexcept;

namespace vk_gltf_viewer::vulkan::gltf {
    export class AssetExtended final : public vk_gltf_viewer::gltf::AssetExtended {
        std::reference_wrapper<const Gpu> gpu;

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
        [[nodiscard]] ImVec2 getTextureSize(std::size_t textureIndex) const override;

        [[nodiscard]] bool isImageLoaded(std::size_t imageIndex) const noexcept override;

        template <bool Mask>
        [[nodiscard]] PrepassPipelineConfig<Mask> getPrepassPipelineConfig(const fastgltf::Primitive &primitive) const {
            const vkgltf::PrimitiveAttributeBuffers &accessors = primitiveAttributeBuffers.at(&primitive);
            PrepassPipelineConfig<Mask> result {
                .positionComponentType = accessors.position.attributeInfo.componentType,
                .positionNormalized = accessors.position.attributeInfo.normalized,
                .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
                .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
            };

            if (!gpu.get().supportDynamicPrimitiveTopologyUnrestricted) {
                result.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
            }

            if constexpr (Mask) {
                result.useTextureTransform = isTextureTransformUsed;

                if (!accessors.colors.empty()) {
                    const fastgltf::Accessor &accessor = asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
                    if (accessor.type == fastgltf::AccessorType::Vec4) {
                        // Alpha value exists only if COLOR_0 is Vec4 type.
                        result.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
                    }
                }

                if (primitive.materialIndex) {
                    const fastgltf::Material &material = asset.materials[*primitive.materialIndex];
                    if (const auto &textureInfo = material.pbrData.baseColorTexture) {
                        const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
                        result.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
                    }
                }
            }

            return result;
        }

        [[nodiscard]] PrimitiveRenderPipeline::Config getPrimitivePipelineConfig(const fastgltf::Primitive &primitive, bool usePerFragmentEmissiveStencilExport) const;
        [[nodiscard]] UnlitPrimitiveRenderPipeline::Config getUnlitPrimitivePipelineConfig(const fastgltf::Primitive &primitive) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk::PrimitiveTopology getListPrimitiveTopology(fastgltf::PrimitiveType type) noexcept {
    switch (type) {
        case fastgltf::PrimitiveType::Points:
            return vk::PrimitiveTopology::ePointList;
        case fastgltf::PrimitiveType::Lines:
        case fastgltf::PrimitiveType::LineLoop:
        case fastgltf::PrimitiveType::LineStrip:
            return vk::PrimitiveTopology::eLineList;
        case fastgltf::PrimitiveType::Triangles:
        case fastgltf::PrimitiveType::TriangleStrip:
        case fastgltf::PrimitiveType::TriangleFan:
            return vk::PrimitiveTopology::eTriangleList;
    }
    std::unreachable();
}

vk_gltf_viewer::vulkan::gltf::AssetExtended::AssetExtended(
    const std::filesystem::path &path,
    const Gpu &gpu,
    const texture::Fallback &fallbackTexture,
    vkgltf::StagingBufferStorage &stagingBufferStorage,
    BS::thread_pool<> threadPool
) : vk_gltf_viewer::gltf::AssetExtended { path },
    gpu { gpu },
    materialBuffer { asset, gpu.allocator, stagingBufferStorage },
    combinedIndexBuffer { asset, gpu.allocator, vkgltf::CombinedIndexBuffer::Config {
        .adapter = externalBuffers,
        .promoteUnsignedByteToUnsignedShort = !gpu.supportUint8Index,
        .topologyConvertFn = [](fastgltf::PrimitiveType type) noexcept {
            if (type == fastgltf::PrimitiveType::LineLoop) {
                return fastgltf::PrimitiveType::LineStrip;
            }
        #if __APPLE__
            if (type == fastgltf::PrimitiveType::TriangleFan) {
                return fastgltf::PrimitiveType::Triangles;
            }
        #endif
            return type;
        },
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

ImVec2 vk_gltf_viewer::vulkan::gltf::AssetExtended::getTextureSize(std::size_t textureIndex) const {
    return imGuiColorSpaceAndUsageCorrectedTextures.getTextureSize(textureIndex);
}

bool vk_gltf_viewer::vulkan::gltf::AssetExtended::isImageLoaded(std::size_t imageIndex) const noexcept {
    return textures.images.contains(imageIndex);
}

vk_gltf_viewer::vulkan::PrimitiveRenderPipeline::Config vk_gltf_viewer::vulkan::gltf::AssetExtended::getPrimitivePipelineConfig(
    const fastgltf::Primitive &primitive,
    bool usePerFragmentEmissiveStencilExport
) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = primitiveAttributeBuffers.at(&primitive);
    PrimitiveRenderPipeline::Config result {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = isTextureTransformUsed,
        .usePerFragmentEmissiveStencilExport = usePerFragmentEmissiveStencilExport,
    };

    if (!gpu.get().supportDynamicPrimitiveTopologyUnrestricted) {
        result.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (accessors.normal) {
        result.normalComponentType = accessors.normal->attributeInfo.componentType;
        result.normalMorphTargetCount = accessors.normal->morphTargets.size();
    }
    else {
        result.fragmentShaderGeneratedTBN = true;
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        result.color0ComponentTypeAndCount.emplace(accessors.colors[0].attributeInfo.componentType, static_cast<std::uint8_t>(getNumComponents(accessor.type)));
    }

    if (primitive.materialIndex) {
        if (accessors.tangent) {
            result.tangentComponentType = accessors.tangent->attributeInfo.componentType;
            result.tangentMorphTargetCount = accessors.tangent->morphTargets.size();
        }

        for (const auto &info : accessors.texcoords) {
            result.texcoordComponentTypeAndNormalized.emplace_back(info.attributeInfo.componentType, info.attributeInfo.normalized);
        }

        const fastgltf::Material &material = asset.materials[*primitive.materialIndex];
        result.alphaMode = material.alphaMode;
    }

    return result;
}

vk_gltf_viewer::vulkan::UnlitPrimitiveRenderPipeline::Config vk_gltf_viewer::vulkan::gltf::AssetExtended::getUnlitPrimitivePipelineConfig(
    const fastgltf::Primitive &primitive
) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = primitiveAttributeBuffers.at(&primitive);
    UnlitPrimitiveRenderPipeline::Config result {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = isTextureTransformUsed,
    };

    if (!gpu.get().supportDynamicPrimitiveTopologyUnrestricted) {
        result.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        result.color0ComponentTypeAndCount.emplace(accessors.colors[0].attributeInfo.componentType, static_cast<std::uint8_t>(getNumComponents(accessor.type)));
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            result.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }

        result.alphaMode = material.alphaMode;
    }

    return result;
}