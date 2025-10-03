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
import vk_gltf_viewer.math.bit;
export import vk_gltf_viewer.vulkan.ag.ImGui;
export import vk_gltf_viewer.vulkan.buffer.CubeIndices;
export import vk_gltf_viewer.vulkan.gltf.AssetExtended;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pipeline.BloomApplyRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.InverseToneMappingRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.JumpFloodComputePipeline;
export import vk_gltf_viewer.vulkan.pipeline.JumpFloodSeedRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.MultiNodeMousePickingRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.NodeMousePickingRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.OutlineRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.PrimitiveRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.SkyboxRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.UnlitPrimitiveRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.WeightedBlendedCompositionRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline_layout.MousePicking;
export import vk_gltf_viewer.vulkan.pipeline_layout.Primitive;
export import vk_gltf_viewer.vulkan.pipeline_layout.PrimitiveNoShading;
export import vk_gltf_viewer.vulkan.render_pass.Scene;

namespace vk_gltf_viewer::vulkan {
    export struct SharedData {
        class ViewMaskDependentResources {
            std::reference_wrapper<const Gpu> gpu;
            std::reference_wrapper<const pl::PrimitiveNoShading> primitiveNoShadingPipelineLayout;

            std::uint32_t viewMask;

        public:
            // TODO: remove mutable
            mutable std::map<PrepassPipelineConfig<false>, JumpFloodSeedRenderPipeline<false>> jumpFloodSeedRenderPipelines;
            mutable std::map<PrepassPipelineConfig<true>, JumpFloodSeedRenderPipeline<true>> maskJumpFloodSeedRenderPipelines;

            ViewMaskDependentResources(
                const Gpu &gpu LIFETIMEBOUND,
                const pl::PrimitiveNoShading &primitiveNoShadingPipelineLayout LIFETIMEBOUND,
                std::uint32_t viewMask
            );

            [[nodiscard]] std::uint32_t getViewMask() const noexcept { return viewMask; }

            // TODO: mark as non-const
            [[nodiscard]] const JumpFloodSeedRenderPipeline<false> &getJumpFloodSeedRenderPipeline(const PrepassPipelineConfig<false> &config) const;
            [[nodiscard]] const JumpFloodSeedRenderPipeline<true> &getMaskJumpFloodSeedRenderPipeline(const PrepassPipelineConfig<true> &config) const;
        };

        const Gpu &gpu;

        // Buffer, image and image views and samplers.
        buffer::CubeIndices cubeIndexBuffer;
        sampler::Cubemap cubemapSampler;
        sampler::BrdfLut brdfLutSampler;

        // Descriptor set layouts.
        dsl::Asset assetDescriptorSetLayout;
        dsl::BloomApply bloomApplyDescriptorSetLayout;
        dsl::ImageBasedLighting imageBasedLightingDescriptorSetLayout;
        dsl::InverseToneMapping inverseToneMappingDescriptorSetLayout;
        dsl::MousePicking mousePickingDescriptorSetLayout;
        dsl::Outline outlineDescriptorSetLayout;
        dsl::Renderer rendererDescriptorSetLayout;
        dsl::Skybox skyboxDescriptorSetLayout;
        dsl::WeightedBlendedComposition weightedBlendedCompositionDescriptorSetLayout;

        // Pipeline layouts.
        pl::BloomApply bloomApplyPipelineLayout;
        pl::InverseToneMapping inverseToneMappingPipelineLayout;
        pl::MousePicking mousePickingPipelineLayout;
        pl::Outline outlinePipelineLayout;
        pl::Primitive primitivePipelineLayout;
        pl::PrimitiveNoShading primitiveNoShadingPipelineLayout;
        pl::Skybox skyboxPipelineLayout;
        pl::WeightedBlendedComposition weightedBlendedCompositionPipelineLayout;

        // --------------------
        // Render passes and pipelines that are independent to the view mask.
        // --------------------

        rp::Scene sceneRenderPass;
        rp::BloomApply bloomApplyRenderPass;

        JumpFloodComputePipeline jumpFloodComputePipeline;
        bloom::BloomComputePipeline bloomComputePipeline;
        OutlineRenderPipeline outlineRenderPipeline;
        SkyboxRenderPipeline skyboxRenderPipeline;
        WeightedBlendedCompositionRenderPipeline weightedBlendedCompositionRenderPipeline;
        InverseToneMappingRenderPipeline inverseToneMappingRenderPipeline;
        BloomApplyRenderPipeline bloomApplyRenderPipeline;

        // TODO: remove mutable
        mutable std::map<PrepassPipelineConfig<false>, NodeMousePickingRenderPipeline<false>> nodeMousePickingRenderPipelines;
        mutable std::map<PrepassPipelineConfig<false>, MultiNodeMousePickingRenderPipeline<false>> multiNodeMousePickingRenderPipelines;
        mutable std::map<PrepassPipelineConfig<true>, NodeMousePickingRenderPipeline<true>> maskNodeMousePickingRenderPipelines;
        mutable std::map<PrepassPipelineConfig<true>, MultiNodeMousePickingRenderPipeline<true>> maskMultiNodeMousePickingRenderPipelines;
        mutable std::map<PrimitiveRenderPipeline::Config, PrimitiveRenderPipeline> primitiveRenderPipelines;
        mutable std::map<UnlitPrimitiveRenderPipeline::Config, UnlitPrimitiveRenderPipeline> unlitPrimitiveRenderPipelines;

        // --------------------
        // Render passes and pipelines that are dependent to the view mask.
        // --------------------

        std::unordered_map<std::uint32_t, ViewMaskDependentResources> viewMaskDependentResources;

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
        // The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
        // submission.
        // --------------------

        void handleSwapchainResize(const vk::Extent2D &newSwapchainExtent, std::span<const vk::Image> newSwapchainImages);

        void setViewCount(std::uint32_t viewCount);

        // TODO: mark as non-const
        [[nodiscard]] const NodeMousePickingRenderPipeline<false> &getNodeMousePickingRenderPipeline(const PrepassPipelineConfig<false> &config) const;
        [[nodiscard]] const MultiNodeMousePickingRenderPipeline<false> &getMultiNodeMousePickingRenderPipeline(const PrepassPipelineConfig<false> &config) const;
        [[nodiscard]] const NodeMousePickingRenderPipeline<true> &getMaskNodeMousePickingRenderPipeline(const PrepassPipelineConfig<true> &config) const;
        [[nodiscard]] const MultiNodeMousePickingRenderPipeline<true> &getMaskMultiNodeMousePickingRenderPipeline(const PrepassPipelineConfig<true> &config) const;
        [[nodiscard]] const PrimitiveRenderPipeline &getPrimitiveRenderPipeline(const PrimitiveRenderPipeline::Config &config) const;
        [[nodiscard]] const UnlitPrimitiveRenderPipeline &getUnlitPrimitiveRenderPipeline(const UnlitPrimitiveRenderPipeline::Config &config) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::SharedData::ViewMaskDependentResources::ViewMaskDependentResources(
    const Gpu &gpu,
    const pl::PrimitiveNoShading &primitiveNoShadingPipelineLayout,
    std::uint32_t viewMask
) : gpu { gpu },
    primitiveNoShadingPipelineLayout { primitiveNoShadingPipelineLayout },
    viewMask { viewMask } { }

auto vk_gltf_viewer::vulkan::SharedData::ViewMaskDependentResources::getJumpFloodSeedRenderPipeline(
    const PrepassPipelineConfig<false> &config
) const -> const JumpFloodSeedRenderPipeline<false>& {
    return jumpFloodSeedRenderPipelines.try_emplace(config, gpu.get().device, primitiveNoShadingPipelineLayout, config, viewMask).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::ViewMaskDependentResources::getMaskJumpFloodSeedRenderPipeline(
    const PrepassPipelineConfig<true> &config
) const -> const JumpFloodSeedRenderPipeline<true>& {
    return maskJumpFloodSeedRenderPipelines.try_emplace(config, gpu.get().device, primitiveNoShadingPipelineLayout, config, viewMask).first->second;
}

vk_gltf_viewer::vulkan::SharedData::SharedData(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages)
    : gpu { gpu }
    , cubeIndexBuffer { gpu.allocator }
    , cubemapSampler { gpu.device }
    , brdfLutSampler { gpu.device }
    , assetDescriptorSetLayout { gpu }
    , bloomApplyDescriptorSetLayout { gpu.device }
    , imageBasedLightingDescriptorSetLayout { gpu.device, cubemapSampler, brdfLutSampler }
    , inverseToneMappingDescriptorSetLayout { gpu.device }
    , mousePickingDescriptorSetLayout { gpu.device }
    , outlineDescriptorSetLayout { gpu.device }
    , rendererDescriptorSetLayout { gpu.device }
    , skyboxDescriptorSetLayout { gpu.device, cubemapSampler }
    , weightedBlendedCompositionDescriptorSetLayout { gpu.device }
    , bloomApplyPipelineLayout { gpu.device, bloomApplyDescriptorSetLayout }
    , inverseToneMappingPipelineLayout { gpu.device, inverseToneMappingDescriptorSetLayout }
    , outlinePipelineLayout { gpu.device, outlineDescriptorSetLayout }
    , mousePickingPipelineLayout { gpu.device, std::tie(rendererDescriptorSetLayout, assetDescriptorSetLayout, mousePickingDescriptorSetLayout) }
    , primitivePipelineLayout { gpu.device, std::tie(rendererDescriptorSetLayout, imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout) }
    , primitiveNoShadingPipelineLayout { gpu.device, std::tie(rendererDescriptorSetLayout, assetDescriptorSetLayout) }
    , skyboxPipelineLayout { gpu.device, { rendererDescriptorSetLayout, skyboxDescriptorSetLayout } }
    , weightedBlendedCompositionPipelineLayout { gpu.device, weightedBlendedCompositionDescriptorSetLayout }
    , sceneRenderPass { gpu }
    , bloomApplyRenderPass { gpu }
    , jumpFloodComputePipeline { gpu.device }
    , bloomComputePipeline { gpu.device, { .useAMDShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod } }
    , outlineRenderPipeline { gpu.device, outlinePipelineLayout }
    , skyboxRenderPipeline { gpu.device, skyboxPipelineLayout, sceneRenderPass }
    , weightedBlendedCompositionRenderPipeline { gpu, weightedBlendedCompositionPipelineLayout, sceneRenderPass }
    , inverseToneMappingRenderPipeline { gpu, inverseToneMappingPipelineLayout, sceneRenderPass }
    , bloomApplyRenderPipeline { gpu, bloomApplyPipelineLayout, bloomApplyRenderPass }
    , imGuiAttachmentGroup { gpu, swapchainExtent, swapchainImages }
    , descriptorPool { gpu.device, getPoolSizes(imageBasedLightingDescriptorSetLayout, skyboxDescriptorSetLayout).getDescriptorPoolCreateInfo() }
    , fallbackTexture { gpu }{
    // Initialize view count dependent resources for viewMask=0b1 at the launch time.
    viewMaskDependentResources.try_emplace(0b1U, gpu, primitiveNoShadingPipelineLayout, 0b1U);

    std::tie(imageBasedLightingDescriptorSet, skyboxDescriptorSet) = vku::allocateDescriptorSets(
        *descriptorPool, std::tie(imageBasedLightingDescriptorSetLayout, skyboxDescriptorSetLayout));
}

// --------------------
// The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
// submission.
// --------------------

void vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages) {
    imGuiAttachmentGroup = { gpu, swapchainExtent, swapchainImages };
}

void vk_gltf_viewer::vulkan::SharedData::setViewCount(std::uint32_t viewCount) {
    const std::uint32_t viewMask = math::bit::ones(viewCount);
    viewMaskDependentResources.try_emplace(viewMask, gpu, primitiveNoShadingPipelineLayout, viewMask);
}

auto vk_gltf_viewer::vulkan::SharedData::getNodeMousePickingRenderPipeline(
    const PrepassPipelineConfig<false> &config
) const -> const NodeMousePickingRenderPipeline<false>& {
    return nodeMousePickingRenderPipelines.try_emplace(config, gpu, mousePickingPipelineLayout, config).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::getMultiNodeMousePickingRenderPipeline(
    const PrepassPipelineConfig<false> &config
) const -> const MultiNodeMousePickingRenderPipeline<false>& {
    return multiNodeMousePickingRenderPipelines.try_emplace(config, gpu, mousePickingPipelineLayout, config).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::getMaskNodeMousePickingRenderPipeline(
    const PrepassPipelineConfig<true> &config
) const -> const NodeMousePickingRenderPipeline<true>& {
    return maskNodeMousePickingRenderPipelines.try_emplace(config, gpu, mousePickingPipelineLayout, config).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::getMaskMultiNodeMousePickingRenderPipeline(
    const PrepassPipelineConfig<true> &config
) const -> const MultiNodeMousePickingRenderPipeline<true>& {
    return maskMultiNodeMousePickingRenderPipelines.try_emplace(config, gpu, mousePickingPipelineLayout, config).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::getPrimitiveRenderPipeline(
    const PrimitiveRenderPipeline::Config &config
) const -> const PrimitiveRenderPipeline& {
    return primitiveRenderPipelines.try_emplace(config, gpu.device, primitivePipelineLayout, sceneRenderPass, config).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::getUnlitPrimitiveRenderPipeline(
    const UnlitPrimitiveRenderPipeline::Config &config
) const -> const UnlitPrimitiveRenderPipeline& {
    return unlitPrimitiveRenderPipelines.try_emplace(config, gpu.device, primitivePipelineLayout, sceneRenderPass, config).first->second;
}