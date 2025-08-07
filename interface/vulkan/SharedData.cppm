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
export import vk_gltf_viewer.vulkan.pipeline_layout.MultiNodeMousePicking;
export import vk_gltf_viewer.vulkan.pipeline_layout.Primitive;
export import vk_gltf_viewer.vulkan.pipeline_layout.PrimitiveNoShading;
export import vk_gltf_viewer.vulkan.render_pass.MousePicking;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan {
    export struct SharedData {
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

        // glTF primitive rendering pipelines.
        // TODO: remove mutable
        mutable std::map<PrepassPipelineConfig<false>, NodeIndexRenderPipeline> nodeIndexRenderPipelines;
        mutable std::map<PrepassPipelineConfig<true>, MaskNodeIndexRenderPipeline> maskNodeIndexRenderPipelines;
        mutable std::map<PrepassPipelineConfig<false>, MultiNodeMousePickingRenderPipeline> multiNodeMousePickingRenderPipelines;
        mutable std::map<PrepassPipelineConfig<true>, MaskMultiNodeMousePickingRenderPipeline> maskMultiNodeMousePickingRenderPipelines;
        mutable std::map<PrepassPipelineConfig<false>, JumpFloodSeedRenderPipeline> jumpFloodSeedRenderingPipelines;
        mutable std::map<PrepassPipelineConfig<true>, MaskJumpFloodSeedRenderPipeline> maskJumpFloodSeedRenderingPipelines;
        mutable std::map<PrimitiveRenderPipeline::Config, PrimitiveRenderPipeline> primitiveRenderPipelines;
        mutable std::map<UnlitPrimitiveRenderPipeline::Config, UnlitPrimitiveRenderPipeline> unlitPrimitiveRenderPipelines;

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
    , assetDescriptorSetLayout { gpu }
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
    PrepassPipelineConfig<false> config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return *nodeIndexRenderPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, mousePickingRenderPass, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskNodeIndexRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    PrepassPipelineConfig<true> config {
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

    return *maskNodeIndexRenderPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, mousePickingRenderPass, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMultiNodeMousePickingRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    PrepassPipelineConfig<false> config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return *multiNodeMousePickingRenderPipelines.try_emplace(config, gpu, multiNodeMousePickingPipelineLayout, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskMultiNodeMousePickingRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    PrepassPipelineConfig<true> config {
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

    return *maskMultiNodeMousePickingRenderPipelines.try_emplace(config, gpu, multiNodeMousePickingPipelineLayout, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getJumpFloodSeedRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    PrepassPipelineConfig<false> config {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        config.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return *jumpFloodSeedRenderingPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, config).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskJumpFloodSeedRenderPipeline(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    PrepassPipelineConfig<true> config {
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

    return *maskJumpFloodSeedRenderingPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, config).first->second;
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

    return *primitiveRenderPipelines.try_emplace(config, gpu.device, primitivePipelineLayout, sceneRenderPass, config).first->second;
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

    return *unlitPrimitiveRenderPipelines.try_emplace(config, gpu.device, primitivePipelineLayout, sceneRenderPass, config).first->second;
}

// --------------------
// The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
// submission.
// --------------------

void vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages) {
    imGuiAttachmentGroup = { gpu, swapchainExtent, swapchainImages };
}