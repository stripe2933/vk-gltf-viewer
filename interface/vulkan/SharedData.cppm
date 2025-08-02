module;

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.SharedData;

import std;
export import bloom;
export import fastgltf;
import imgui.vulkan;
export import vku;

import vk_gltf_viewer.helpers.AggregateHasher;
import vk_gltf_viewer.helpers.fastgltf;
export import vk_gltf_viewer.vulkan.ag.ImGui;
export import vk_gltf_viewer.vulkan.gltf.AssetExtended;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pipeline.BloomApplyRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.InverseToneMappingRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.JumpFloodComputePipeline;
import vk_gltf_viewer.vulkan.pipeline.JumpFloodSeedRenderPipeline;
import vk_gltf_viewer.vulkan.pipeline.MaskJumpFloodSeedRenderPipeline;
import vk_gltf_viewer.vulkan.pipeline.MaskMultiNodeMousePickingRenderPipeline;
import vk_gltf_viewer.vulkan.pipeline.MaskNodeIndexRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.MousePickingRenderPipeline;
import vk_gltf_viewer.vulkan.pipeline.MultiNodeMousePickingRenderPipeline;
import vk_gltf_viewer.vulkan.pipeline.NodeIndexRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.OutlineRenderPipeline;
import vk_gltf_viewer.vulkan.pipeline.PrimitiveRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.SkyboxRenderPipeline;
import vk_gltf_viewer.vulkan.pipeline.UnlitPrimitiveRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.WeightedBlendedCompositionRenderPipeline;
export import vk_gltf_viewer.vulkan.pl.MultiNodeMousePicking;
export import vk_gltf_viewer.vulkan.pl.Primitive;
export import vk_gltf_viewer.vulkan.pl.PrimitiveNoShading;
export import vk_gltf_viewer.vulkan.rp.MousePicking;
export import vk_gltf_viewer.vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
    public:
        const Gpu &gpu;

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
        JumpFloodComputePipeline jumpFloodComputePipeline;
        MousePickingRenderPipeline mousePickingRenderPipeline;
        OutlineRenderPipeline outlineRenderPipeline;
        SkyboxRenderPipeline skyboxRenderPipeline;
        WeightedBlendedCompositionRenderPipeline weightedBlendedCompositionRenderPipeline;
        InverseToneMappingRenderPipeline inverseToneMappingRenderPipeline;
        bloom::BloomComputePipeline bloomComputePipeline;
        BloomApplyRenderPipeline bloomApplyRenderPipeline;

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
        std::shared_ptr<const vulkan::gltf::AssetExtended> assetExtended;

        SharedData(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages);

        // --------------------
        // Pipeline selectors.
        // --------------------

        [[nodiscard]] vk::Pipeline getNodeIndexRenderPipeline(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getMaskNodeIndexRenderPipeline(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getMultiNodeMousePickingRenderPipeline(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getMaskMultiNodeMousePickingRenderPipeline(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getJumpFloodSeedRenderPipeline(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getMaskJumpFloodSeedRenderPipeline(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getPrimitiveRenderPipeline(const fastgltf::Primitive &primitive, bool usePerFragmentEmissiveStencilExport) const;
        [[nodiscard]] vk::Pipeline getUnlitPrimitiveRenderPipeline(const fastgltf::Primitive &primitive) const;

        // --------------------
        // The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
        // submission.
        // --------------------

        void handleSwapchainResize(const vk::Extent2D &newSwapchainExtent, std::span<const vk::Image> newSwapchainImages);

        void setAsset(std::shared_ptr<const vulkan::gltf::AssetExtended> assetExtended);

    private:
        // --------------------
        // Pipelines.
        // --------------------

        // glTF primitive rendering pipelines.
        mutable std::unordered_map<NodeIndexRenderPipeline::Config, NodeIndexRenderPipeline, AggregateHasher> nodeIndexPipelines;
        mutable std::unordered_map<MaskNodeIndexRenderPipeline::Config, MaskNodeIndexRenderPipeline, AggregateHasher> maskNodeIndexPipelines;
        mutable std::unordered_map<MultiNodeMousePickingRenderPipeline::Config, MultiNodeMousePickingRenderPipeline, AggregateHasher> multiNodeMousePickingPipelines;
        mutable std::unordered_map<MaskMultiNodeMousePickingRenderPipeline::Config, MaskMultiNodeMousePickingRenderPipeline, AggregateHasher> maskMultiNodeMousePickingPipelines;
        mutable std::unordered_map<JumpFloodSeedRenderPipeline::Config, JumpFloodSeedRenderPipeline, AggregateHasher> jumpFloodSeedPipelines;
        mutable std::unordered_map<MaskJumpFloodSeedRenderPipeline::Config, MaskJumpFloodSeedRenderPipeline, AggregateHasher> maskJumpFloodSeedPipelines;
        mutable std::unordered_map<PrimitiveRenderPipeline::Config, PrimitiveRenderPipeline, AggregateHasher> primitivePipelines;
        mutable std::unordered_map<UnlitPrimitiveRenderPipeline::Config, UnlitPrimitiveRenderPipeline, AggregateHasher> unlitPrimitivePipelines;
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

vk_gltf_viewer::vulkan::SharedData::SharedData(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages)
    : gpu { gpu }
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
    , jumpFloodComputePipeline { gpu.device }
    , mousePickingRenderPipeline { gpu.device, mousePickingRenderPass }
    , outlineRenderPipeline { gpu.device }
    , skyboxRenderPipeline { gpu.device, skyboxDescriptorSetLayout, sceneRenderPass, cubeIndices }
    , weightedBlendedCompositionRenderPipeline { gpu, sceneRenderPass }
    , inverseToneMappingRenderPipeline { gpu, sceneRenderPass }
    , bloomComputePipeline { gpu.device, { .useAMDShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod } }
    , bloomApplyRenderPipeline { gpu, bloomApplyRenderPass }
    , imGuiAttachmentGroup { gpu, swapchainExtent, swapchainImages }
    , descriptorPool { gpu.device, getPoolSizes(imageBasedLightingDescriptorSetLayout, skyboxDescriptorSetLayout).getDescriptorPoolCreateInfo() }
    , fallbackTexture { gpu }{
    std::tie(imageBasedLightingDescriptorSet, skyboxDescriptorSet) = vku::allocateDescriptorSets(
        *descriptorPool, std::tie(imageBasedLightingDescriptorSetLayout, skyboxDescriptorSetLayout));
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getNodeIndexRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    NodeIndexRenderPipeline::Config config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return *nodeIndexPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, mousePickingRenderPass, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskNodeIndexRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    MaskNodeIndexRenderPipeline::Config config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetExtended->isTextureTransformUsed,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        if (accessor.type == fastgltf::AccessorType::Vec4) {
            // Alpha value exists only if COLOR_0 is Vec4 type.
            config.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
        }
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            config.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }
    }

    return *maskNodeIndexPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, mousePickingRenderPass, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMultiNodeMousePickingRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    MultiNodeMousePickingRenderPipeline::Config config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return *multiNodeMousePickingPipelines.try_emplace(config, gpu, multiNodeMousePickingPipelineLayout, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskMultiNodeMousePickingRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    MaskMultiNodeMousePickingRenderPipeline::Config config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetExtended->isTextureTransformUsed,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        if (accessor.type == fastgltf::AccessorType::Vec4) {
            // Alpha value exists only if COLOR_0 is Vec4 type.
            config.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
        }
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            config.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }
    }

    return *maskMultiNodeMousePickingPipelines.try_emplace(config, gpu, multiNodeMousePickingPipelineLayout, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getJumpFloodSeedRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    JumpFloodSeedRenderPipeline::Config config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return *jumpFloodSeedPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskJumpFloodSeedRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    MaskJumpFloodSeedRenderPipeline::Config config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetExtended->isTextureTransformUsed,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        if (accessor.type == fastgltf::AccessorType::Vec4) {
            // Alpha value exists only if COLOR_0 is Vec4 type.
            config.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
        }
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            config.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }
    }

    return *maskJumpFloodSeedPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getPrimitiveRenderPipeline(const fastgltf::Primitive &primitive, bool usePerFragmentEmissiveStencilExport) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    PrimitiveRenderPipeline::Config config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetExtended->isTextureTransformUsed,
        .usePerFragmentEmissiveStencilExport = usePerFragmentEmissiveStencilExport,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (accessors.normal) {
        config.normalComponentType = accessors.normal->attributeInfo.componentType;
        config.normalMorphTargetCount = accessors.normal->morphTargets.size();
    }
    else {
        config.fragmentShaderGeneratedTBN = true;
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        config.color0ComponentTypeAndCount.emplace(accessors.colors[0].attributeInfo.componentType, static_cast<std::uint8_t>(getNumComponents(accessor.type)));
    }

    if (primitive.materialIndex) {
        if (accessors.tangent) {
            config.tangentComponentType = accessors.tangent->attributeInfo.componentType;
            config.tangentMorphTargetCount = accessors.tangent->morphTargets.size();
        }

        for (const auto &info : accessors.texcoords) {
            config.texcoordComponentTypeAndNormalized.emplace_back(info.attributeInfo.componentType, info.attributeInfo.normalized);
        }

        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        config.alphaMode = material.alphaMode;
    }

    return *primitivePipelines.try_emplace(config, gpu.device, primitivePipelineLayout, sceneRenderPass, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getUnlitPrimitiveRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    UnlitPrimitiveRenderPipeline::Config config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
        .useTextureTransform = assetExtended->isTextureTransformUsed,
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    if (!accessors.colors.empty()) {
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        config.color0ComponentTypeAndCount.emplace(accessors.colors[0].attributeInfo.componentType, static_cast<std::uint8_t>(getNumComponents(accessor.type)));
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            config.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }

        config.alphaMode = material.alphaMode;
    }

    return *unlitPrimitivePipelines.try_emplace(config, gpu.device, primitivePipelineLayout, sceneRenderPass, config).first->second;
}

// --------------------
// The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
// submission.
// --------------------

void vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages) {
    imGuiAttachmentGroup = { gpu, swapchainExtent, swapchainImages };
}

void vk_gltf_viewer::vulkan::SharedData::setAsset(std::shared_ptr<const vulkan::gltf::AssetExtended> _assetExtended) {
    assetExtended = std::move(_assetExtended);

    const std::uint32_t textureCount = 1 + assetExtended->asset.textures.size();
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
}