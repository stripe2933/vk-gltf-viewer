module;

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.SharedData;

import std;
export import bloom;
export import fastgltf;
import imgui.vulkan;
export import vkgltf.bindless;
export import vku;

export import vk_gltf_viewer.gltf.AssetExternalBuffers;
import vk_gltf_viewer.helpers.AggregateHasher;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.vulkan.ag.ImGui;
export import vk_gltf_viewer.vulkan.buffer.Materials;
export import vk_gltf_viewer.vulkan.buffer.PrimitiveAttributes;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pipeline.AssetSpecialization;
export import vk_gltf_viewer.vulkan.pipeline.BloomApplyRenderer;
export import vk_gltf_viewer.vulkan.pipeline.InverseToneMappingRenderer;
export import vk_gltf_viewer.vulkan.pipeline.JumpFloodComputer;
import vk_gltf_viewer.vulkan.pipeline.JumpFloodSeedRenderer;
import vk_gltf_viewer.vulkan.pipeline.MaskJumpFloodSeedRenderer;
import vk_gltf_viewer.vulkan.pipeline.MaskMultiNodeMousePickingRenderer;
import vk_gltf_viewer.vulkan.pipeline.MaskNodeIndexRenderer;
export import vk_gltf_viewer.vulkan.pipeline.MousePickingRenderer;
export import vk_gltf_viewer.vulkan.pipeline.MultiNodeMousePickingRenderer;
import vk_gltf_viewer.vulkan.pipeline.NodeIndexRenderer;
export import vk_gltf_viewer.vulkan.pipeline.OutlineRenderer;
import vk_gltf_viewer.vulkan.pipeline.PrimitiveRenderer;
export import vk_gltf_viewer.vulkan.pipeline.SkyboxRenderer;
import vk_gltf_viewer.vulkan.pipeline.UnlitPrimitiveRenderer;
export import vk_gltf_viewer.vulkan.pipeline.WeightedBlendedCompositionRenderer;
export import vk_gltf_viewer.vulkan.pl.MultiNodeMousePicking;
export import vk_gltf_viewer.vulkan.pl.Primitive;
export import vk_gltf_viewer.vulkan.pl.PrimitiveNoShading;
export import vk_gltf_viewer.vulkan.rp.MousePicking;
export import vk_gltf_viewer.vulkan.rp.Scene;
export import vk_gltf_viewer.vulkan.texture.ImGuiColorSpaceAndUsageCorrectedTextures;
export import vk_gltf_viewer.vulkan.texture.Textures;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
    public:
        struct GltfAsset {
            const fastgltf::Asset &asset;

            buffer::Materials materialBuffer;
            vkgltf::CombinedIndexBuffer combinedIndexBuffer;
            std::unordered_map<const fastgltf::Primitive*, vkgltf::PrimitiveAttributeBuffers> primitiveAttributeBuffers;
            vkgltf::PrimitiveBuffer primitiveBuffer;
            std::optional<vkgltf::SkinBuffer> skinBuffer;
            texture::Textures textures;
            texture::ImGuiColorSpaceAndUsageCorrectedTextures imGuiColorSpaceAndUsageCorrectedTextures;

            std::vector<vk::DescriptorSet> imGuiTextureDescriptorSets;

            GltfAsset(
                const fastgltf::Asset &asset,
                const std::filesystem::path &directory,
                const Gpu &gpu,
                const texture::Fallback &fallbackTexture,
                const gltf::AssetExternalBuffers &adapter,
                vkgltf::StagingBufferStorage &stagingBufferStorage,
                BS::thread_pool<> threadPool = {}
            );
            ~GltfAsset();
        };

        const Gpu &gpu;

        // --------------------
        // Non-owning swapchain resources.
        // --------------------

        vk::Extent2D swapchainExtent;
        std::span<const vk::Image> swapchainImages;

        // Buffer, image and image views and samplers.
        buffer::CubeIndices cubeIndices;
        sampler::Cubemap cubemapSampler;
        sampler::BrdfLut brdfLutSampler;

        // Descriptor set layouts.
        dsl::Asset assetDescriptorSetLayout;
        dsl::ImageBasedLighting imageBasedLightingDescriptorSetLayout;
        dsl::MultiNodeMousePicking multiNodeMousePickingDescriptorSetLayout;
        dsl::Skybox skyboxDescriptorSetLayout;

        // Render passes.
        rp::MousePicking mousePickingRenderPass;
        rp::Scene sceneRenderPass;
        rp::BloomApply bloomApplyRenderPass;

        // Pipeline layouts.
        pl::MultiNodeMousePicking multiNodeMousePickingPipelineLayout;
        pl::Primitive primitivePipelineLayout;
        pl::PrimitiveNoShading primitiveNoShadingPipelineLayout;

        // --------------------
        // Pipelines.
        // --------------------

        // Primitive unrelated pipelines.
        JumpFloodComputer jumpFloodComputer;
        MousePickingRenderer mousePickingRenderer;
        OutlineRenderer outlineRenderer;
        SkyboxRenderer skyboxRenderer;
        WeightedBlendedCompositionRenderer weightedBlendedCompositionRenderer;
        InverseToneMappingRenderer inverseToneMappingRenderer;
        bloom::BloomComputer bloomComputer;
        BloomApplyRenderer bloomApplyRenderer;

        // --------------------
        // Attachment groups.
        // --------------------

        ag::ImGui imGuiAttachmentGroup;

        // Descriptor pools.
        vk::raii::DescriptorPool descriptorPool;

        // Descriptor sets.
        vku::DescriptorSet<dsl::ImageBasedLighting> imageBasedLightingDescriptorSet;
        vku::DescriptorSet<dsl::Skybox> skyboxDescriptorSet;

        // --------------------
        // glTF assets.
        // --------------------

        texture::Fallback fallbackTexture;
        std::optional<GltfAsset> gltfAsset;

        SharedData(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages);

        // --------------------
        // Pipeline selectors.
        // --------------------

        [[nodiscard]] vk::Pipeline getNodeIndexRenderer(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getMaskNodeIndexRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getMultiNodeMousePickingRenderer(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getMaskMultiNodeMousePickingRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getJumpFloodSeedRenderer(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getMaskJumpFloodSeedRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getPrimitiveRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive, bool usePerFragmentEmissiveStencilExport) const;
        [[nodiscard]] vk::Pipeline getUnlitPrimitiveRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const;

        // --------------------
        // The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
        // submission.
        // --------------------

        void handleSwapchainResize(const vk::Extent2D &newSwapchainExtent, std::span<const vk::Image> newSwapchainImages);

        void changeAsset(
            const fastgltf::Asset &asset,
            const std::filesystem::path &directory,
            const gltf::AssetExternalBuffers &adapter
        );

    private:
        // --------------------
        // Pipelines.
        // --------------------

        // glTF primitive rendering pipelines.
        mutable std::unordered_map<NodeIndexRendererSpecialization, vk::raii::Pipeline, AggregateHasher> nodeIndexPipelines;
        mutable std::unordered_map<MaskNodeIndexRendererSpecialization, vk::raii::Pipeline, AggregateHasher> maskNodeIndexPipelines;
        mutable std::unordered_map<MultiNodeMousePickingRendererSpecialization, vk::raii::Pipeline, AggregateHasher> multiNodeMousePickingPipelines;
        mutable std::unordered_map<MaskMultiNodeMousePickingRendererSpecialization, vk::raii::Pipeline, AggregateHasher> maskMultiNodeMousePickingPipelines;
        mutable std::unordered_map<JumpFloodSeedRendererSpecialization, vk::raii::Pipeline, AggregateHasher> jumpFloodSeedPipelines;
        mutable std::unordered_map<MaskJumpFloodSeedRendererSpecialization, vk::raii::Pipeline, AggregateHasher> maskJumpFloodSeedPipelines;
        mutable std::unordered_map<PrimitiveRendererSpecialization, vk::raii::Pipeline, AggregateHasher> primitivePipelines;
        mutable std::unordered_map<UnlitPrimitiveRendererSpecialization, vk::raii::Pipeline, AggregateHasher> unlitPrimitivePipelines;
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

vk_gltf_viewer::vulkan::SharedData::GltfAsset::GltfAsset(
    const fastgltf::Asset &asset,
    const std::filesystem::path &directory,
    const Gpu &gpu,
    const texture::Fallback &fallbackTexture,
    const gltf::AssetExternalBuffers &adapter,
    vkgltf::StagingBufferStorage &stagingBufferStorage,
    BS::thread_pool<> threadPool
) : asset { asset },
    materialBuffer { asset, gpu.allocator, stagingBufferStorage },
    combinedIndexBuffer { asset, gpu.allocator, vkgltf::CombinedIndexBuffer::Config {
        .adapter = adapter,
        .promoteUnsignedByteToUnsignedShort = !gpu.supportUint8Index,
        .usageFlags = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferSrc,
        .queueFamilies = gpu.queueFamilies.uniqueIndices,
        .stagingInfo = vku::unsafeAddress(vkgltf::StagingInfo { stagingBufferStorage }),
    } },
    primitiveAttributeBuffers { buffer::createPrimitiveAttributeBuffers(asset, gpu, stagingBufferStorage, threadPool, adapter) },
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
        .adapter = adapter,
        .usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
        .queueFamilies = gpu.queueFamilies.uniqueIndices,
        .stagingInfo = vku::unsafeAddress(vkgltf::StagingInfo { stagingBufferStorage }),
    }) },
    textures { asset, directory, gpu, fallbackTexture, threadPool, adapter },
    imGuiColorSpaceAndUsageCorrectedTextures { asset, textures, gpu } {
    imGuiTextureDescriptorSets
        = textures.descriptorInfos
        | std::views::transform([](const vk::DescriptorImageInfo &descriptorInfo) -> vk::DescriptorSet {
            return ImGui_ImplVulkan_AddTexture(descriptorInfo.sampler, descriptorInfo.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        })
        | std::ranges::to<std::vector>();
}

vk_gltf_viewer::vulkan::SharedData::GltfAsset::~GltfAsset() {
    std::ranges::for_each(imGuiTextureDescriptorSets, ImGui_ImplVulkan_RemoveTexture);
}

vk_gltf_viewer::vulkan::SharedData::SharedData(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages)
    : gpu { gpu }
    , swapchainExtent { swapchainExtent }
    , swapchainImages { swapchainImages }
    , cubeIndices { gpu.allocator }
    , cubemapSampler { gpu.device }
    , brdfLutSampler { gpu.device }
    , assetDescriptorSetLayout { [&]() {
        if (gpu.supportVariableDescriptorCount) {
            return dsl::Asset { gpu };
        }
        else {
            return dsl::Asset { gpu, 1 }; // TODO: set proper initial texture count.
        }
    }() }
    , imageBasedLightingDescriptorSetLayout { gpu.device, cubemapSampler, brdfLutSampler }
    , multiNodeMousePickingDescriptorSetLayout { gpu.device }
    , skyboxDescriptorSetLayout { gpu.device, cubemapSampler }
    , mousePickingRenderPass { gpu.device }
    , sceneRenderPass { gpu }
    , bloomApplyRenderPass { gpu }
    , multiNodeMousePickingPipelineLayout { gpu.device, std::tie(assetDescriptorSetLayout, multiNodeMousePickingDescriptorSetLayout) }
    , primitivePipelineLayout { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout) }
    , primitiveNoShadingPipelineLayout { gpu.device, assetDescriptorSetLayout }
    , jumpFloodComputer { gpu.device }
    , mousePickingRenderer { gpu.device, mousePickingRenderPass }
    , outlineRenderer { gpu.device }
    , skyboxRenderer { gpu.device, skyboxDescriptorSetLayout, sceneRenderPass, cubeIndices }
    , weightedBlendedCompositionRenderer { gpu, sceneRenderPass }
    , inverseToneMappingRenderer { gpu, sceneRenderPass }
    , bloomComputer { gpu.device, { .useAMDShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod } }
    , bloomApplyRenderer { gpu, bloomApplyRenderPass }
    , imGuiAttachmentGroup { gpu, swapchainExtent, swapchainImages }
    , descriptorPool { gpu.device, getPoolSizes(imageBasedLightingDescriptorSetLayout, skyboxDescriptorSetLayout).getDescriptorPoolCreateInfo() }
    , fallbackTexture { gpu }{
    std::tie(imageBasedLightingDescriptorSet, skyboxDescriptorSet) = vku::allocateDescriptorSets(
        *descriptorPool, std::tie(imageBasedLightingDescriptorSetLayout, skyboxDescriptorSetLayout));
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getNodeIndexRenderer(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = gltfAsset->primitiveAttributeBuffers.at(&primitive);
    NodeIndexRendererSpecialization specialization {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        specialization.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return ranges::try_emplace_if_not_exists(nodeIndexPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, primitiveNoShadingPipelineLayout, mousePickingRenderPass);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskNodeIndexRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = gltfAsset->primitiveAttributeBuffers.at(&primitive);
    MaskNodeIndexRendererSpecialization specialization {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetSpecialization.useTextureTransform,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        specialization.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = gltfAsset->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        if (accessor.type == fastgltf::AccessorType::Vec4) {
            // Alpha value exists only if COLOR_0 is Vec4 type.
            specialization.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
        }
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = gltfAsset->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            specialization.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }
    }

    return ranges::try_emplace_if_not_exists(maskNodeIndexPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, primitiveNoShadingPipelineLayout, mousePickingRenderPass);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMultiNodeMousePickingRenderer(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = gltfAsset->primitiveAttributeBuffers.at(&primitive);
    MultiNodeMousePickingRendererSpecialization specialization {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        specialization.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return ranges::try_emplace_if_not_exists(multiNodeMousePickingPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu, multiNodeMousePickingPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskMultiNodeMousePickingRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = gltfAsset->primitiveAttributeBuffers.at(&primitive);
    MaskMultiNodeMousePickingRendererSpecialization specialization {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetSpecialization.useTextureTransform,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        specialization.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = gltfAsset->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        if (accessor.type == fastgltf::AccessorType::Vec4) {
            // Alpha value exists only if COLOR_0 is Vec4 type.
            specialization.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
        }
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = gltfAsset->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            specialization.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }
    }

    return ranges::try_emplace_if_not_exists(maskMultiNodeMousePickingPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu, multiNodeMousePickingPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getJumpFloodSeedRenderer(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = gltfAsset->primitiveAttributeBuffers.at(&primitive);
    JumpFloodSeedRendererSpecialization specialization {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        specialization.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return ranges::try_emplace_if_not_exists(jumpFloodSeedPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, primitiveNoShadingPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskJumpFloodSeedRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = gltfAsset->primitiveAttributeBuffers.at(&primitive);
    MaskJumpFloodSeedRendererSpecialization specialization {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetSpecialization.useTextureTransform,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        specialization.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = gltfAsset->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        if (accessor.type == fastgltf::AccessorType::Vec4) {
            // Alpha value exists only if COLOR_0 is Vec4 type.
            specialization.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
        }
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = gltfAsset->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            specialization.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }
    }

    return ranges::try_emplace_if_not_exists(maskJumpFloodSeedPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, primitiveNoShadingPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getPrimitiveRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive, bool usePerFragmentEmissiveStencilExport) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = gltfAsset->primitiveAttributeBuffers.at(&primitive);
    PrimitiveRendererSpecialization specialization {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetSpecialization.useTextureTransform,
        .usePerFragmentEmissiveStencilExport = usePerFragmentEmissiveStencilExport,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        specialization.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (accessors.normal) {
        specialization.normalComponentType = accessors.normal->attributeInfo.componentType;
        specialization.normalMorphTargetCount = accessors.normal->morphTargets.size();
    }
    else {
        specialization.fragmentShaderGeneratedTBN = true;
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = gltfAsset->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        specialization.color0ComponentTypeAndCount.emplace(accessors.colors[0].attributeInfo.componentType, static_cast<std::uint8_t>(getNumComponents(accessor.type)));
    }

    if (primitive.materialIndex) {
        if (accessors.tangent) {
            specialization.tangentComponentType = accessors.tangent->attributeInfo.componentType;
            specialization.tangentMorphTargetCount = accessors.tangent->morphTargets.size();
        }

        for (const auto &info : accessors.texcoords) {
            specialization.texcoordComponentTypeAndNormalized.emplace_back(info.attributeInfo.componentType, info.attributeInfo.normalized);
        }

        const fastgltf::Material &material = gltfAsset->asset.materials[*primitive.materialIndex];
        specialization.alphaMode = material.alphaMode;
    }

    return ranges::try_emplace_if_not_exists(primitivePipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, primitivePipelineLayout, sceneRenderPass);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getUnlitPrimitiveRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = gltfAsset->primitiveAttributeBuffers.at(&primitive);
    UnlitPrimitiveRendererSpecialization specialization {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetSpecialization.useTextureTransform,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        specialization.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = gltfAsset->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        specialization.color0ComponentTypeAndCount.emplace(accessors.colors[0].attributeInfo.componentType, static_cast<std::uint8_t>(getNumComponents(accessor.type)));
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = gltfAsset->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            specialization.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }

        specialization.alphaMode = material.alphaMode;
    }

    return ranges::try_emplace_if_not_exists(unlitPrimitivePipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, primitivePipelineLayout, sceneRenderPass);
    }).first->second;
}

// --------------------
// The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
// submission.
// --------------------

void vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(const vk::Extent2D &newSwapchainExtent, std::span<const vk::Image> newSwapchainImages) {
    swapchainExtent = newSwapchainExtent;
    swapchainImages = newSwapchainImages;

    imGuiAttachmentGroup = { gpu, swapchainExtent, swapchainImages };
}

void vk_gltf_viewer::vulkan::SharedData::changeAsset(
    const fastgltf::Asset &asset,
    const std::filesystem::path &directory,
    const gltf::AssetExternalBuffers &adapter
) {
    // If asset texture count exceeds the available texture count provided by the GPU, throw the error before
    // processing data to avoid unnecessary processing.
    const std::uint32_t textureCount = 1 + asset.textures.size();
    if (textureCount > dsl::Asset::maxTextureCount(gpu)) {
        throw gltf::AssetProcessError::TooManyTextureError;
    }

    vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
    vkgltf::StagingBufferStorage stagingBufferStorage { gpu.device, transferCommandPool, gpu.queues.transfer };
    gltfAsset.emplace(asset, directory, gpu, fallbackTexture, adapter, stagingBufferStorage);

    if (!gpu.supportVariableDescriptorCount && get<3>(assetDescriptorSetLayout.descriptorCounts) != textureCount) {
        // If texture count is different, descriptor set layouts, pipeline layouts and pipelines have to be recreated.
        nodeIndexPipelines.clear();
        maskNodeIndexPipelines.clear();
        multiNodeMousePickingPipelines.clear();
        maskMultiNodeMousePickingPipelines.clear();
        jumpFloodSeedPipelines.clear();
        maskJumpFloodSeedPipelines.clear();
        primitivePipelines.clear();
        unlitPrimitivePipelines.clear();

        assetDescriptorSetLayout = { gpu, textureCount };
        multiNodeMousePickingPipelineLayout = { gpu.device, std::tie(assetDescriptorSetLayout, multiNodeMousePickingDescriptorSetLayout) };
        primitivePipelineLayout = { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout) };
        primitiveNoShadingPipelineLayout = { gpu.device, assetDescriptorSetLayout };
    }

    // In here stagingBufferStorage will be destroyed and fence will block until buffer staging operation finishes.
}