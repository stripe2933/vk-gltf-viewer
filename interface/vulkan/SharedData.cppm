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
export import vk_gltf_viewer.vulkan.gltf.AssetExtended;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pipeline.BloomApplyRenderPipeline;
export import vk_gltf_viewer.vulkan.pipeline.GridRenderPipeline;
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
export import vk_gltf_viewer.vulkan.Swapchain;

namespace vk_gltf_viewer::vulkan {
    export struct SharedData {
        class MultiviewPipelines {
            std::reference_wrapper<const vk::raii::Device> device;
            std::reference_wrapper<const pl::PrimitiveNoShading> primitiveNoShadingPipelineLayout;

            std::uint32_t viewMask;

        public:
            // TODO: remove mutable
            mutable std::map<PrepassPipelineConfig<false>, JumpFloodSeedRenderPipeline<false>> jumpFloodSeedRenderPipelines;
            mutable std::map<PrepassPipelineConfig<true>, JumpFloodSeedRenderPipeline<true>> maskJumpFloodSeedRenderPipelines;

            MultiviewPipelines(
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

        rp::Scene sceneRenderPass;
        rp::BloomApply bloomApplyRenderPass;

        GridRenderPipeline gridRenderPipeline;
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

        std::unordered_map<std::uint32_t, MultiviewPipelines> multiviewPipelines;

        // --------------------
        // Attachment groups.
        // --------------------

        Swapchain swapchain;
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

        SharedData(const Gpu &gpu LIFETIMEBOUND, vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent);

        // --------------------
        // The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
        // submission.
        // --------------------

        void handleSwapchainResize(const vk::Extent2D &newExtent);

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

vk_gltf_viewer::vulkan::SharedData::MultiviewPipelines::MultiviewPipelines(
    const Gpu &gpu,
    const pl::PrimitiveNoShading &primitiveNoShadingPipelineLayout,
    std::uint32_t viewMask
) : device { gpu.device },
    primitiveNoShadingPipelineLayout { primitiveNoShadingPipelineLayout },
    viewMask { viewMask } { }

auto vk_gltf_viewer::vulkan::SharedData::MultiviewPipelines::getJumpFloodSeedRenderPipeline(
    const PrepassPipelineConfig<false> &config
) const -> const JumpFloodSeedRenderPipeline<false>& {
    return jumpFloodSeedRenderPipelines.try_emplace(config, device, primitiveNoShadingPipelineLayout, config, viewMask).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::MultiviewPipelines::getMaskJumpFloodSeedRenderPipeline(
    const PrepassPipelineConfig<true> &config
) const -> const JumpFloodSeedRenderPipeline<true>& {
    return maskJumpFloodSeedRenderPipelines.try_emplace(config, device, primitiveNoShadingPipelineLayout, config, viewMask).first->second;
}

vk_gltf_viewer::vulkan::SharedData::SharedData(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent)
    : gpu { gpu }
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
    , sceneRenderPass { gpu, vk::SampleCountFlagBits::e1 }
    , bloomApplyRenderPass { gpu }
    , gridRenderPipeline { gpu.device, rendererDescriptorSetLayout, sceneRenderPass }
    , jumpFloodComputePipeline { gpu.device }
    , bloomComputePipeline { gpu.device, { .useAMDShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod } }
    , outlineRenderPipeline { gpu.device, outlinePipelineLayout }
    , skyboxRenderPipeline { gpu.device, skyboxPipelineLayout, sceneRenderPass }
    , weightedBlendedCompositionRenderPipeline { gpu, weightedBlendedCompositionPipelineLayout, sceneRenderPass }
    , inverseToneMappingRenderPipeline { gpu, inverseToneMappingPipelineLayout, sceneRenderPass }
    , bloomApplyRenderPipeline { gpu, bloomApplyPipelineLayout, bloomApplyRenderPass }
    , swapchain { gpu, surface, swapchainExtent }
    , imGuiAttachmentGroup { gpu, swapchain.images }
    , descriptorPool { [&] {
        const auto [maxSets, poolSizes] = vku::DescriptorPoolSizeBuilder{}
            .add(imageBasedLightingDescriptorSetLayout)
            .add(skyboxDescriptorSetLayout)
            .build();
        return vk::raii::DescriptorPool { gpu.device, vk::DescriptorPoolCreateInfo { {}, maxSets, poolSizes } };
    }() }
    , fallbackTexture { gpu }{
    // Initialize view count dependent resources for viewMask=0b1 at the launch time.
    multiviewPipelines.try_emplace(0b1U, gpu, primitiveNoShadingPipelineLayout, 0b1U);

    vku::DescriptorSetAllocationBuilder{}
        .add(imageBasedLightingDescriptorSetLayout, imageBasedLightingDescriptorSet)
        .add(skyboxDescriptorSetLayout, skyboxDescriptorSet)
        .allocate(gpu.device, *descriptorPool);
}

// --------------------
// The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
// submission.
// --------------------

void vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(const vk::Extent2D &extent) {
    swapchain.setExtent(extent);
    imGuiAttachmentGroup = { gpu, swapchain.images };
}

void vk_gltf_viewer::vulkan::SharedData::setViewCount(std::uint32_t viewCount) {
    const std::uint32_t viewMask = math::bit::ones(viewCount);
    multiviewPipelines.try_emplace(viewMask, gpu, primitiveNoShadingPipelineLayout, viewMask);
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