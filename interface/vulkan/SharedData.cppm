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
import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.vulkan.ag.ImGui;
export import vk_gltf_viewer.vulkan.gltf.AssetExtended;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pipeline.BloomApplyRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.InverseToneMappingRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.JumpFloodComputePipeline;
export import vk_gltf_viewer.vulkan.pipeline.JumpFloodSeedRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.MultiNodeMousePickingRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.NodeMousePickingRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.OutlineRenderPipeline;
import vk_gltf_viewer.vulkan.pipeline.PrimitiveRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.SkyboxRenderPipeline;
import vk_gltf_viewer.vulkan.pipeline.UnlitPrimitiveRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.WeightedBlendedCompositionRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline_layout.MousePicking;
export import vk_gltf_viewer.vulkan.pipeline_layout.Primitive;
export import vk_gltf_viewer.vulkan.pipeline_layout.PrimitiveNoShading;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan {
    export struct SharedData {
        template <bool Mask>
        struct PrepassPipelines {
            NodeMousePickingRenderPipeline<Mask> nodeMousePickingRenderPipeline;
            MultiNodeMousePickingRenderPipeline<Mask> multiNodeMousePickingRenderPipeline;
            JumpFloodSeedRenderPipeline<Mask> jumpFloodSeedRenderingPipeline;
        };

        const Gpu &gpu;

        // Buffer, image and image views and samplers.
        buffer::CubeIndices cubeIndices;
        sampler::Cubemap cubemapSampler;
        sampler::BrdfLut brdfLutSampler;

        // Descriptor set layouts.
        dsl::Asset assetDescriptorSetLayout;
        dsl::ImageBasedLighting imageBasedLightingDescriptorSetLayout;
        dsl::MousePicking mousePickingDescriptorSetLayout;
        dsl::Skybox skyboxDescriptorSetLayout;

        // Render passes.
        rp::Scene sceneRenderPass;
        rp::BloomApply bloomApplyRenderPass;

        // Pipeline layouts.
        pl::MousePicking mousePickingPipelineLayout;
        pl::Primitive primitivePipelineLayout;
        pl::PrimitiveNoShading primitiveNoShadingPipelineLayout;

        // --------------------
        // Pipelines.
        // --------------------

        // Primitive unrelated pipelines.
        JumpFloodComputePipeline jumpFloodComputePipeline;
        OutlineRenderPipeline outlineRenderPipeline;
        SkyboxRenderPipeline skyboxRenderPipeline;
        WeightedBlendedCompositionRenderPipeline weightedBlendedCompositionRenderPipeline;
        InverseToneMappingRenderPipeline inverseToneMappingRenderPipeline;
        bloom::BloomComputePipeline bloomComputePipeline;
        BloomApplyRenderPipeline bloomApplyRenderPipeline;

        // glTF primitive rendering pipelines.
        // TODO: remove mutable
        mutable std::map<PrepassPipelineConfig<false>, PrepassPipelines<false>> prepassPipelines;
        mutable std::map<PrepassPipelineConfig<true>, PrepassPipelines<true>> maskPrepassPipelines;
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
        std::shared_ptr<const gltf::AssetExtended> assetExtended;

        SharedData(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages);

        // --------------------
        // Pipeline selectors.
        // --------------------

        [[nodiscard]] const PrepassPipelines<false> &getPrepassPipelines(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] const PrepassPipelines<true> &getMaskPrepassPipelines(const fastgltf::Primitive &primitive) const;
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
    , mousePickingDescriptorSetLayout { gpu.device }
    , skyboxDescriptorSetLayout { gpu.device, cubemapSampler }
    , sceneRenderPass { gpu }
    , bloomApplyRenderPass { gpu }
    , mousePickingPipelineLayout { gpu.device, std::tie(assetDescriptorSetLayout, mousePickingDescriptorSetLayout) }
    , primitivePipelineLayout { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout) }
    , primitiveNoShadingPipelineLayout { gpu.device, assetDescriptorSetLayout }
    , jumpFloodComputePipeline { gpu.device }
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

auto vk_gltf_viewer::vulkan::SharedData::getPrepassPipelines(
    const fastgltf::Primitive &primitive
) const -> const PrepassPipelines<false>& {
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

    return ranges::try_emplace_if_not_exists(prepassPipelines, config, [&] -> PrepassPipelines<false> {
        return {
            .nodeMousePickingRenderPipeline = { gpu.device, mousePickingPipelineLayout, config },
            .multiNodeMousePickingRenderPipeline = { gpu, mousePickingPipelineLayout, config },
            .jumpFloodSeedRenderingPipeline = { gpu.device, primitiveNoShadingPipelineLayout, config },
        };
    }).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::getMaskPrepassPipelines(
    const fastgltf::Primitive &primitive
) const -> const PrepassPipelines<true>& {
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

    return ranges::try_emplace_if_not_exists(maskPrepassPipelines, config, [&] -> PrepassPipelines<true> {
        return {
            .nodeMousePickingRenderPipeline = { gpu.device, mousePickingPipelineLayout, config },
            .multiNodeMousePickingRenderPipeline = { gpu, mousePickingPipelineLayout, config },
            .jumpFloodSeedRenderingPipeline = { gpu.device, primitiveNoShadingPipelineLayout, config },
        };
    }).first->second;
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