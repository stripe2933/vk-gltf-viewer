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
import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.vulkan.ag.ImGui;
export import vk_gltf_viewer.vulkan.buffer.CubeIndices;
export import vk_gltf_viewer.vulkan.gltf.AssetExtended;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pipeline.AssetSpecialization;
export import vk_gltf_viewer.vulkan.pipeline.BloomApplyRenderer;
export import vk_gltf_viewer.vulkan.pipeline.InverseToneMappingRenderer;
export import vk_gltf_viewer.vulkan.pipeline.JumpFloodComputer;
import vk_gltf_viewer.vulkan.pipeline.JumpFloodSeedRenderer;
import vk_gltf_viewer.vulkan.pipeline.MaskJumpFloodSeedRenderer;
import vk_gltf_viewer.vulkan.pipeline.MaskMultiNodeMousePickingRenderer;
import vk_gltf_viewer.vulkan.pipeline.MaskMousePickingRenderer;
import vk_gltf_viewer.vulkan.pipeline.MousePickingRenderer;
import vk_gltf_viewer.vulkan.pipeline.MultiNodeMousePickingRenderer;
export import vk_gltf_viewer.vulkan.pipeline.OutlineRenderer;
import vk_gltf_viewer.vulkan.pipeline.PrimitiveRenderer;
export import vk_gltf_viewer.vulkan.pipeline.SkyboxRenderer;
import vk_gltf_viewer.vulkan.pipeline.UnlitPrimitiveRenderer;
export import vk_gltf_viewer.vulkan.pipeline.WeightedBlendedCompositionRenderer;
export import vk_gltf_viewer.vulkan.pl.MousePicking;
export import vk_gltf_viewer.vulkan.pl.Primitive;
export import vk_gltf_viewer.vulkan.pl.PrimitiveMultiview;
export import vk_gltf_viewer.vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
    public:
        const Gpu &gpu;

        // Buffer, image and image views and samplers.
        buffer::CubeIndices cubeIndexBuffer;
        sampler::Cubemap cubemapSampler;
        sampler::BrdfLut brdfLutSampler;

        // Descriptor set layouts.
        dsl::Renderer rendererDescriptorSetLayout;
        dsl::Asset assetDescriptorSetLayout;
        dsl::MousePicking mousePickingDescriptorSetLayout;

        // Render passes.
        rp::Scene sceneRenderPass;
        rp::BloomApply bloomApplyRenderPass;

        // Pipeline layouts.
        pl::MousePicking mousePickingPipelineLayout;
        pl::Primitive primitivePipelineLayout;
        pl::PrimitiveMultiview primitiveMultiviewPipelineLayout;

        // --------------------
        // Pipelines.
        // --------------------

        // Primitive unrelated pipelines.
        JumpFloodComputer jumpFloodComputer;
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

        // --------------------
        // glTF assets.
        // --------------------

        texture::Fallback fallbackTexture;
        std::shared_ptr<const vulkan::gltf::AssetExtended> assetExtended;

        SharedData(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages);

        // --------------------
        // Pipeline selectors.
        // --------------------

        [[nodiscard]] vk::Pipeline getMousePickingRenderer(const fastgltf::Primitive &primitive) const;
        [[nodiscard]] vk::Pipeline getMaskMousePickingRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const;
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

        void setAsset(std::shared_ptr<const vulkan::gltf::AssetExtended> assetExtended);

    private:
        // --------------------
        // Pipelines.
        // --------------------

        // glTF primitive rendering pipelines.
        mutable std::unordered_map<MousePickingRendererSpecialization, vk::raii::Pipeline, AggregateHasher> mousePickingPipelines;
        mutable std::unordered_map<MaskMousePickingRendererSpecialization, vk::raii::Pipeline, AggregateHasher> maskMousePickingPipelines;
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

vk_gltf_viewer::vulkan::SharedData::SharedData(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages)
    : gpu { gpu }
    , cubeIndexBuffer { gpu.allocator }
    , cubemapSampler { gpu.device }
    , brdfLutSampler { gpu.device }
    , rendererDescriptorSetLayout { gpu.device, cubemapSampler, brdfLutSampler }
    , assetDescriptorSetLayout { [&]() {
        if (gpu.supportVariableDescriptorCount) {
            return dsl::Asset { gpu };
        }
        else {
            return dsl::Asset { gpu, 1 }; // TODO: set proper initial texture count.
        }
    }() }
    , mousePickingDescriptorSetLayout { gpu.device }
    , sceneRenderPass { gpu }
    , bloomApplyRenderPass { gpu }
    , mousePickingPipelineLayout { gpu.device, std::tie(rendererDescriptorSetLayout, assetDescriptorSetLayout, mousePickingDescriptorSetLayout) }
    , primitivePipelineLayout { gpu.device, std::tie(rendererDescriptorSetLayout, assetDescriptorSetLayout) }
    , primitiveMultiviewPipelineLayout { gpu.device, std::tie(rendererDescriptorSetLayout, assetDescriptorSetLayout) }
    , jumpFloodComputer { gpu.device }
    , outlineRenderer { gpu.device }
    , skyboxRenderer { gpu.device, cubemapSampler, sceneRenderPass }
    , weightedBlendedCompositionRenderer { gpu, sceneRenderPass }
    , inverseToneMappingRenderer { gpu, sceneRenderPass }
    , bloomComputer { gpu.device, { .useAMDShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod } }
    , bloomApplyRenderer { gpu, bloomApplyRenderPass }
    , imGuiAttachmentGroup { gpu, swapchainExtent, swapchainImages }
    , fallbackTexture { gpu } { }

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMousePickingRenderer(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    MousePickingRendererSpecialization specialization {
        .positionComponentType = accessors.position.attributeInfo.componentType,
        .positionNormalized = accessors.position.attributeInfo.normalized,
        .positionMorphTargetCount = static_cast<std::uint32_t>(accessors.position.morphTargets.size()),
        .skinAttributeCount = static_cast<std::uint32_t>(accessors.joints.size()),
    };

    if (!gpu.supportDynamicPrimitiveTopologyUnrestricted) {
        specialization.topologyClass.emplace(getListPrimitiveTopology(primitive.type));
    }

    return ranges::try_emplace_if_not_exists(mousePickingPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, mousePickingPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskMousePickingRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
    MaskMousePickingRendererSpecialization specialization {
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
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        if (accessor.type == fastgltf::AccessorType::Vec4) {
            // Alpha value exists only if COLOR_0 is Vec4 type.
            specialization.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
        }
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            specialization.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }
    }

    return ranges::try_emplace_if_not_exists(maskMousePickingPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, mousePickingPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMultiNodeMousePickingRenderer(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
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
        return specialization.createPipeline(gpu, mousePickingPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskMultiNodeMousePickingRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
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
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        if (accessor.type == fastgltf::AccessorType::Vec4) {
            // Alpha value exists only if COLOR_0 is Vec4 type.
            specialization.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
        }
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            specialization.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }
    }

    return ranges::try_emplace_if_not_exists(maskMultiNodeMousePickingPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu, mousePickingPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getJumpFloodSeedRenderer(const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
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
        return specialization.createPipeline(gpu.device, primitiveMultiviewPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getMaskJumpFloodSeedRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
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
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        if (accessor.type == fastgltf::AccessorType::Vec4) {
            // Alpha value exists only if COLOR_0 is Vec4 type.
            specialization.color0AlphaComponentType.emplace(accessors.colors[0].attributeInfo.componentType);
        }
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            specialization.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }
    }

    return ranges::try_emplace_if_not_exists(maskJumpFloodSeedPipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, primitiveMultiviewPipelineLayout);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getPrimitiveRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive, bool usePerFragmentEmissiveStencilExport) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
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
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
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

        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        specialization.alphaMode = material.alphaMode;
    }

    return ranges::try_emplace_if_not_exists(primitivePipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, primitiveMultiviewPipelineLayout, sceneRenderPass);
    }).first->second;
}

vk::Pipeline vk_gltf_viewer::vulkan::SharedData::getUnlitPrimitiveRenderer(const AssetSpecialization &assetSpecialization, const fastgltf::Primitive &primitive) const {
    const vkgltf::PrimitiveAttributeBuffers &accessors = assetExtended->primitiveAttributeBuffers.at(&primitive);
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
        const fastgltf::Accessor &accessor = assetExtended->asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
        specialization.color0ComponentTypeAndCount.emplace(accessors.colors[0].attributeInfo.componentType, static_cast<std::uint8_t>(getNumComponents(accessor.type)));
    }

    if (primitive.materialIndex) {
        const fastgltf::Material &material = assetExtended->asset.materials[*primitive.materialIndex];
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            const vkgltf::PrimitiveAttributeBuffers::AttributeInfo &info = accessors.texcoords.at(getTexcoordIndex(*textureInfo)).attributeInfo;
            specialization.baseColorTexcoordComponentTypeAndNormalized.emplace(info.componentType, info.normalized);
        }

        specialization.alphaMode = material.alphaMode;
    }

    return ranges::try_emplace_if_not_exists(unlitPrimitivePipelines, specialization, [&]() {
        return specialization.createPipeline(gpu.device, primitiveMultiviewPipelineLayout, sceneRenderPass);
    }).first->second;
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
        mousePickingPipelines.clear();
        maskMousePickingPipelines.clear();
        multiNodeMousePickingPipelines.clear();
        maskMultiNodeMousePickingPipelines.clear();
        jumpFloodSeedPipelines.clear();
        maskJumpFloodSeedPipelines.clear();
        primitivePipelines.clear();
        unlitPrimitivePipelines.clear();

        assetDescriptorSetLayout = { gpu, textureCount };
        mousePickingPipelineLayout = { gpu.device, std::tie(rendererDescriptorSetLayout, assetDescriptorSetLayout, mousePickingDescriptorSetLayout) };
        primitivePipelineLayout = { gpu.device, std::tie(rendererDescriptorSetLayout, assetDescriptorSetLayout) };
        primitiveMultiviewPipelineLayout = { gpu.device, std::tie(rendererDescriptorSetLayout, assetDescriptorSetLayout) };
    }
}