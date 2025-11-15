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
    export class SharedData {
    public:
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

        rp::BloomApply bloomApplyRenderPass;

        JumpFloodComputePipeline jumpFloodComputePipeline;
        bloom::BloomComputePipeline bloomComputePipeline;
        OutlineRenderPipeline outlineRenderPipeline;
        BloomApplyRenderPipeline bloomApplyRenderPipeline;

    private:
        struct MultisamplePipelines {
            rp::Scene sceneRenderPass;

            GridRenderPipeline gridRenderPipeline;
            SkyboxRenderPipeline skyboxRenderPipeline;
            WeightedBlendedCompositionRenderPipeline weightedBlendedCompositionRenderPipeline;
            InverseToneMappingRenderPipeline inverseToneMappingRenderPipeline;

            std::map<PrimitiveRenderPipeline::Config, PrimitiveRenderPipeline> primitiveRenderPipelines;
            std::map<UnlitPrimitiveRenderPipeline::Config, UnlitPrimitiveRenderPipeline> unlitPrimitiveRenderPipelines;
        };

        struct MultiviewPipelines {
            std::map<PrepassPipelineConfig<false>, JumpFloodSeedRenderPipeline<false>> jumpFloodSeedRenderPipelines;
            std::map<PrepassPipelineConfig<true>, JumpFloodSeedRenderPipeline<true>> maskJumpFloodSeedRenderPipelines;
        };

        std::uint32_t viewMask;

        // TODO: remove mutable
        mutable std::map<PrepassPipelineConfig<false>, NodeMousePickingRenderPipeline<false>> nodeMousePickingRenderPipelines;
        mutable std::map<PrepassPipelineConfig<false>, MultiNodeMousePickingRenderPipeline<false>> multiNodeMousePickingRenderPipelines;
        mutable std::map<PrepassPipelineConfig<true>, NodeMousePickingRenderPipeline<true>> maskNodeMousePickingRenderPipelines;
        mutable std::map<PrepassPipelineConfig<true>, MultiNodeMousePickingRenderPipeline<true>> maskMultiNodeMousePickingRenderPipelines;

        std::unordered_map<vk::SampleCountFlagBits, MultisamplePipelines> multisamplePipelines;
        std::reference_wrapper<MultisamplePipelines> currentMultisamplePipelines;

        std::unordered_map<std::uint32_t /* view mask */, MultiviewPipelines> multiviewPipelines;
        std::reference_wrapper<MultiviewPipelines> currentMultiviewPipelines;

    public:
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

        void setSampleCount(vk::SampleCountFlagBits sampleCount);
        void setViewCount(std::uint32_t viewCount);

        [[nodiscard]] const rp::Scene &getSceneRenderPass() const {
            return currentMultisamplePipelines.get().sceneRenderPass;
        }

        [[nodiscard]] const GridRenderPipeline &getGridRenderPipeline() const {
            return currentMultisamplePipelines.get().gridRenderPipeline;
        }

        [[nodiscard]] const SkyboxRenderPipeline &getSkyboxRenderPipeline() const {
            return currentMultisamplePipelines.get().skyboxRenderPipeline;
        }

        [[nodiscard]] const WeightedBlendedCompositionRenderPipeline &getWeightedBlendedCompositionRenderPipeline() const {
            return currentMultisamplePipelines.get().weightedBlendedCompositionRenderPipeline;
        }

        [[nodiscard]] const InverseToneMappingRenderPipeline &getInverseToneMappingRenderPipeline() const {
            return currentMultisamplePipelines.get().inverseToneMappingRenderPipeline;
        }

        // TODO: mark as non-const
        [[nodiscard]] const NodeMousePickingRenderPipeline<false> &getNodeMousePickingRenderPipeline(const PrepassPipelineConfig<false> &config) const;
        [[nodiscard]] const MultiNodeMousePickingRenderPipeline<false> &getMultiNodeMousePickingRenderPipeline(const PrepassPipelineConfig<false> &config) const;
        [[nodiscard]] const NodeMousePickingRenderPipeline<true> &getMaskNodeMousePickingRenderPipeline(const PrepassPipelineConfig<true> &config) const;
        [[nodiscard]] const MultiNodeMousePickingRenderPipeline<true> &getMaskMultiNodeMousePickingRenderPipeline(const PrepassPipelineConfig<true> &config) const;
        [[nodiscard]] const JumpFloodSeedRenderPipeline<false> &getJumpFloodSeedRenderPipeline(const PrepassPipelineConfig<false> &config) const;
        [[nodiscard]] const JumpFloodSeedRenderPipeline<true> &getMaskJumpFloodSeedRenderPipeline(const PrepassPipelineConfig<true> &config) const;
        [[nodiscard]] const PrimitiveRenderPipeline &getPrimitiveRenderPipeline(const PrimitiveRenderPipeline::Config &config) const;
        [[nodiscard]] const UnlitPrimitiveRenderPipeline &getUnlitPrimitiveRenderPipeline(const UnlitPrimitiveRenderPipeline::Config &config) const;

    private:
        [[nodiscard]] MultisamplePipelines createMultisamplePipelines(vk::SampleCountFlagBits sampleCount) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

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
    , bloomApplyRenderPass { gpu }
    , jumpFloodComputePipeline { gpu.device }
    , bloomComputePipeline { gpu.device, { .useAMDShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod } }
    , outlineRenderPipeline { gpu.device, outlinePipelineLayout }
    , bloomApplyRenderPipeline { gpu, bloomApplyPipelineLayout, bloomApplyRenderPass }
    , viewMask { 0b1U }
    , multisamplePipelines { [&] {
        decltype(multisamplePipelines) result;
        result.try_emplace(vk::SampleCountFlagBits::e1, createMultisamplePipelines(vk::SampleCountFlagBits::e1));
        return result;
    }() }
    , currentMultisamplePipelines { multisamplePipelines.at(vk::SampleCountFlagBits::e1) }
    , currentMultiviewPipelines { multiviewPipelines[viewMask] } // will create an entry for viewMask = 0b1
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

void vk_gltf_viewer::vulkan::SharedData::setSampleCount(vk::SampleCountFlagBits sampleCount) {
    currentMultisamplePipelines = multisamplePipelines.try_emplace(sampleCount, createMultisamplePipelines(sampleCount)).first->second;
}

void vk_gltf_viewer::vulkan::SharedData::setViewCount(std::uint32_t viewCount) {
    viewMask = math::bit::ones(viewCount);
    currentMultiviewPipelines = multiviewPipelines[viewMask];
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

auto vk_gltf_viewer::vulkan::SharedData::getJumpFloodSeedRenderPipeline(
    const PrepassPipelineConfig<false> &config
) const -> const JumpFloodSeedRenderPipeline<false>& {
    return currentMultiviewPipelines.get().jumpFloodSeedRenderPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, config, viewMask).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::getMaskJumpFloodSeedRenderPipeline(
    const PrepassPipelineConfig<true> &config
) const -> const JumpFloodSeedRenderPipeline<true>& {
    return currentMultiviewPipelines.get().maskJumpFloodSeedRenderPipelines.try_emplace(config, gpu.device, primitiveNoShadingPipelineLayout, config, viewMask).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::getPrimitiveRenderPipeline(
    const PrimitiveRenderPipeline::Config &config
) const -> const PrimitiveRenderPipeline& {
    return currentMultisamplePipelines.get().primitiveRenderPipelines.try_emplace(config, gpu.device, primitivePipelineLayout, currentMultisamplePipelines.get().sceneRenderPass, config).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::getUnlitPrimitiveRenderPipeline(
    const UnlitPrimitiveRenderPipeline::Config &config
) const -> const UnlitPrimitiveRenderPipeline& {
    return currentMultisamplePipelines.get().unlitPrimitiveRenderPipelines.try_emplace(config, gpu.device, primitivePipelineLayout, currentMultisamplePipelines.get().sceneRenderPass, config).first->second;
}

auto vk_gltf_viewer::vulkan::SharedData::createMultisamplePipelines(vk::SampleCountFlagBits sampleCount) const -> MultisamplePipelines {
    rp::Scene sceneRenderPass { gpu, sampleCount };
    GridRenderPipeline gridRenderPipeline { gpu.device, rendererDescriptorSetLayout, sceneRenderPass };
    SkyboxRenderPipeline skyboxRenderPipeline { gpu.device, skyboxPipelineLayout, sceneRenderPass };
    WeightedBlendedCompositionRenderPipeline weightedBlendedCompositionRenderPipeline { gpu, weightedBlendedCompositionPipelineLayout, sceneRenderPass };
    InverseToneMappingRenderPipeline inverseToneMappingRenderPipeline { gpu, inverseToneMappingPipelineLayout, sceneRenderPass };

    return {
        .sceneRenderPass = std::move(sceneRenderPass),
        .gridRenderPipeline = std::move(gridRenderPipeline),
        .skyboxRenderPipeline = std::move(skyboxRenderPipeline),
        .weightedBlendedCompositionRenderPipeline = std::move(weightedBlendedCompositionRenderPipeline),
        .inverseToneMappingRenderPipeline = std::move(inverseToneMappingRenderPipeline),
    };
}