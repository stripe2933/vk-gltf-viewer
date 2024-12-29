module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.SharedData;

import std;
export import fastgltf;
export import vku;
export import :vulkan.ag.Swapchain;
export import :vulkan.Gpu;
export import :vulkan.pipeline.BlendPrimitiveRenderer;
export import :vulkan.pipeline.BlendUnlitPrimitiveRenderer;
export import :vulkan.pipeline.DepthRenderer;
export import :vulkan.pipeline.JumpFloodComputer;
export import :vulkan.pipeline.JumpFloodSeedRenderer;
export import :vulkan.pipeline.MaskDepthRenderer;
export import :vulkan.pipeline.MaskJumpFloodSeedRenderer;
export import :vulkan.pipeline.MaskPrimitiveRenderer;
export import :vulkan.pipeline.MaskUnlitPrimitiveRenderer;
export import :vulkan.pipeline.OutlineRenderer;
export import :vulkan.pipeline.PrimitiveRenderer;
export import :vulkan.pipeline.SkyboxRenderer;
export import :vulkan.pipeline.UnlitPrimitiveRenderer;
export import :vulkan.pipeline.WeightedBlendedCompositionRenderer;
export import :vulkan.rp.Scene;
import :vulkan.sampler.SingleTexelSampler;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
        const Gpu &gpu;

    public:
        // --------------------
        // Non-owning swapchain resources.
        // --------------------

        vk::Extent2D swapchainExtent;
        std::span<const vk::Image> swapchainImages;

        // Buffer, image and image views and samplers.
        buffer::CubeIndices cubeIndices { gpu.allocator };
        CubemapSampler cubemapSampler { gpu.device };
        BrdfLutSampler brdfLutSampler { gpu.device };
        SingleTexelSampler singleTexelSampler { gpu.device };

        // Descriptor set layouts.
        dsl::Asset assetDescriptorSetLayout { gpu.device, 1 }; // TODO: set proper initial texture count.
        dsl::ImageBasedLighting imageBasedLightingDescriptorSetLayout { gpu.device, cubemapSampler, brdfLutSampler };
        dsl::Scene sceneDescriptorSetLayout { gpu.device };
        dsl::Skybox skyboxDescriptorSetLayout { gpu.device, cubemapSampler };

        // Render passes.
        rp::Scene sceneRenderPass { gpu.device };

        // Pipeline layouts.
        pl::Primitive primitivePipelineLayout { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout, sceneDescriptorSetLayout) };
        pl::PrimitiveNoShading primitiveNoShadingPipelineLayout { gpu.device, std::tie(assetDescriptorSetLayout, sceneDescriptorSetLayout) };

        // Pipelines.
        BlendPrimitiveRenderer blendFacetedPrimitiveRenderer { gpu.device, primitivePipelineLayout, sceneRenderPass, true };
        BlendPrimitiveRenderer blendPrimitiveRenderer { gpu.device, primitivePipelineLayout, sceneRenderPass, false };
        BlendUnlitPrimitiveRenderer blendUnlitPrimitiveRenderer { gpu.device, primitivePipelineLayout, sceneRenderPass };
        DepthRenderer depthRenderer { gpu.device, primitiveNoShadingPipelineLayout };
        PrimitiveRenderer facetedPrimitiveRenderer { gpu.device, primitivePipelineLayout, sceneRenderPass, true };
        JumpFloodComputer jumpFloodComputer { gpu.device };
        JumpFloodSeedRenderer jumpFloodSeedRenderer { gpu.device, primitiveNoShadingPipelineLayout };
        MaskDepthRenderer maskDepthRenderer { gpu.device, primitiveNoShadingPipelineLayout };
        MaskPrimitiveRenderer maskFacetedPrimitiveRenderer { gpu.device, primitivePipelineLayout, sceneRenderPass, true };
        MaskJumpFloodSeedRenderer maskJumpFloodSeedRenderer { gpu.device, primitiveNoShadingPipelineLayout };
        MaskPrimitiveRenderer maskPrimitiveRenderer { gpu.device, primitivePipelineLayout, sceneRenderPass, false };
        MaskUnlitPrimitiveRenderer maskUnlitPrimitiveRenderer { gpu.device, primitivePipelineLayout, sceneRenderPass };
        OutlineRenderer outlineRenderer { gpu.device };
        PrimitiveRenderer primitiveRenderer { gpu.device, primitivePipelineLayout, sceneRenderPass, false };
        SkyboxRenderer skyboxRenderer { gpu.device, skyboxDescriptorSetLayout, true, sceneRenderPass, cubeIndices };
        UnlitPrimitiveRenderer unlitPrimitiveRenderer { gpu.device, primitivePipelineLayout, sceneRenderPass };
        WeightedBlendedCompositionRenderer weightedBlendedCompositionRenderer { gpu.device, sceneRenderPass };

        // --------------------
        // Attachment groups.
        // --------------------

        ag::Swapchain swapchainAttachmentGroup { gpu, swapchainExtent, swapchainImages };
        // If GPU does not support mutable swapchain format, it will be the reference of swapchainAttachmentGroup.
        std::variant<ag::Swapchain, std::reference_wrapper<ag::Swapchain>> imGuiSwapchainAttachmentGroup = getImGuiSwapchainAttachmentGroup();

        // Descriptor pools.
        vk::raii::DescriptorPool textureDescriptorPool = createTextureDescriptorPool();
        vk::raii::DescriptorPool descriptorPool = createDescriptorPool();

        // Descriptor sets.
        vku::DescriptorSet<dsl::Asset> assetDescriptorSet;
        vku::DescriptorSet<dsl::Scene> sceneDescriptorSet;
        vku::DescriptorSet<dsl::ImageBasedLighting> imageBasedLightingDescriptorSet;
        vku::DescriptorSet<dsl::Skybox> skyboxDescriptorSet;

        SharedData(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages)
            : gpu { gpu }
            , swapchainExtent { swapchainExtent }
            , swapchainImages { swapchainImages } {
            std::tie(assetDescriptorSet)
                = vku::allocateDescriptorSets(*gpu.device, *textureDescriptorPool, std::tie(
                    assetDescriptorSetLayout));
            std::tie(sceneDescriptorSet, imageBasedLightingDescriptorSet, skyboxDescriptorSet)
                = vku::allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(
                    sceneDescriptorSetLayout,
                    imageBasedLightingDescriptorSetLayout,
                    skyboxDescriptorSetLayout));
        }

        /**
         * @brief Get corresponding Vulkan pipeline for given \p material and TBN generation policy.
         * @param material Material of primitive to render.
         * @param fragmentShaderTBN Flag whether to generate the TBN matrix in the fragment shader or not. If rendering primitive is non-indexed geometry, this flag MUST be set to <tt>true</tt>.
         * @return Vulkan pipeline for the given material.
         */
        [[nodiscard]] vk::Pipeline getPrimitiveRenderer(const fastgltf::Material &material, bool fragmentShaderTBN) const noexcept {
            switch (material.alphaMode) {
                case fastgltf::AlphaMode::Opaque:
                    return material.unlit
                        ? *unlitPrimitiveRenderer
                        : fragmentShaderTBN
                            ? *facetedPrimitiveRenderer
                            : *primitiveRenderer;
                case fastgltf::AlphaMode::Mask:
                    return material.unlit
                        ? *maskUnlitPrimitiveRenderer
                        : fragmentShaderTBN
                            ? *maskFacetedPrimitiveRenderer
                            : *maskPrimitiveRenderer;
                case fastgltf::AlphaMode::Blend:
                    return material.unlit
                        ? *blendUnlitPrimitiveRenderer
                        : fragmentShaderTBN
                            ? *blendFacetedPrimitiveRenderer
                            : *blendPrimitiveRenderer;
            }
        }

        /**
         * @brief Get corresponding Vulkan pipeline for depth prepass rendering.
         * @param alphaMode Alpha mode of the material.
         * @return Vulkan pipeline for depth prepass rendering.
         */
        [[nodiscard]] vk::Pipeline getDepthPrepassRenderer(fastgltf::AlphaMode alphaMode) const noexcept {
            return alphaMode == fastgltf::AlphaMode::Mask? *maskDepthRenderer : *depthRenderer;
        }

        /**
         * @brief Get corresponding Vulkan pipeline for jump flood seeding.
         * @param alphaMode Alpha mode of the material.
         * @return Vulkan pipeline for jump flood seeding.
         */
        [[nodiscard]] vk::Pipeline getJumpFloodSeedRenderer(fastgltf::AlphaMode alphaMode) const noexcept {
            return alphaMode == fastgltf::AlphaMode::Mask? *maskJumpFloodSeedRenderer : *jumpFloodSeedRenderer;
        }

        // --------------------
        // The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
        // submission.
        // --------------------

        void handleSwapchainResize(const vk::Extent2D &newSwapchainExtent, std::span<const vk::Image> newSwapchainImages) {
            swapchainExtent = newSwapchainExtent;
            swapchainImages = newSwapchainImages;

            swapchainAttachmentGroup = { gpu, swapchainExtent, swapchainImages };
            imGuiSwapchainAttachmentGroup = getImGuiSwapchainAttachmentGroup();
        }

        auto updateTextureCount(std::uint32_t textureCount) -> void {
            assetDescriptorSetLayout = { gpu.device, textureCount };
            primitivePipelineLayout = { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout, sceneDescriptorSetLayout) };
            primitiveNoShadingPipelineLayout = { gpu.device, std::tie(assetDescriptorSetLayout, sceneDescriptorSetLayout) };

            // Following pipelines are dependent to the assetDescriptorSetLayout.
            blendFacetedPrimitiveRenderer = { gpu.device, primitivePipelineLayout, sceneRenderPass, true };
            blendPrimitiveRenderer = { gpu.device, primitivePipelineLayout, sceneRenderPass, false };
            blendUnlitPrimitiveRenderer = { gpu.device, primitivePipelineLayout, sceneRenderPass };
            depthRenderer = { gpu.device, primitiveNoShadingPipelineLayout };
            facetedPrimitiveRenderer = { gpu.device, primitivePipelineLayout, sceneRenderPass, true };
            jumpFloodSeedRenderer = { gpu.device, primitiveNoShadingPipelineLayout };
            maskDepthRenderer = { gpu.device, primitiveNoShadingPipelineLayout };
            maskFacetedPrimitiveRenderer = { gpu.device, primitivePipelineLayout, sceneRenderPass, true };
            maskJumpFloodSeedRenderer = { gpu.device, primitiveNoShadingPipelineLayout };
            maskPrimitiveRenderer = { gpu.device, primitivePipelineLayout, sceneRenderPass, false };
            maskUnlitPrimitiveRenderer = { gpu.device, primitivePipelineLayout, sceneRenderPass };
            primitiveRenderer = { gpu.device, primitivePipelineLayout, sceneRenderPass, false };
            unlitPrimitiveRenderer = { gpu.device, primitivePipelineLayout, sceneRenderPass };

            textureDescriptorPool = createTextureDescriptorPool();
            std::tie(assetDescriptorSet) = vku::allocateDescriptorSets(*gpu.device, *textureDescriptorPool, std::tie(assetDescriptorSetLayout));
        }

    private:
        [[nodiscard]] std::variant<ag::Swapchain, std::reference_wrapper<ag::Swapchain>> getImGuiSwapchainAttachmentGroup() {
            if (gpu.supportSwapchainMutableFormat) {
                return decltype(imGuiSwapchainAttachmentGroup) { std::in_place_index<0>, gpu, swapchainExtent, swapchainImages, vk::Format::eB8G8R8A8Unorm };
            }
            else {
                return decltype(imGuiSwapchainAttachmentGroup) { std::in_place_index<1>, swapchainAttachmentGroup };
            }
        }

        [[nodiscard]] auto createTextureDescriptorPool() const -> vk::raii::DescriptorPool {
            return { gpu.device, getPoolSizes(assetDescriptorSetLayout).getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind) };
        }

        [[nodiscard]] auto createDescriptorPool() const -> vk::raii::DescriptorPool {
            return { gpu.device, getPoolSizes(imageBasedLightingDescriptorSetLayout, sceneDescriptorSetLayout, skyboxDescriptorSetLayout).getDescriptorPoolCreateInfo() };
        }
    };
}
