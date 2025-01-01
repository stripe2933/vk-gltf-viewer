module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.SharedData;

import std;
export import fastgltf;
export import vku;
import :helpers.AggregateHasher;
export import :vulkan.ag.Swapchain;
export import :vulkan.Gpu;
export import :vulkan.pipeline.DepthRenderer;
export import :vulkan.pipeline.JumpFloodComputer;
export import :vulkan.pipeline.JumpFloodSeedRenderer;
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
        struct PrimitivePipelineKey {
            bool unlit;
            std::uint8_t texcoordCount;
            bool fragmentShaderGeneratedTBN;
            fastgltf::AlphaMode alphaMode;

            [[nodiscard]] constexpr std::strong_ordering operator<=>(const PrimitivePipelineKey&) const noexcept = default;
        };

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

        // --------------------
        // Pipelines.
        // --------------------

        // Primitive unrelated pipelines.
        JumpFloodComputer jumpFloodComputer { gpu.device };
        OutlineRenderer outlineRenderer { gpu.device };
        SkyboxRenderer skyboxRenderer { gpu.device, skyboxDescriptorSetLayout, true, sceneRenderPass, cubeIndices };
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
         * @brief Get corresponding Vulkan pipeline for given key.
         * @param key A key that represents the primitive's rendering policy (alpha mode, normal/tangent space generation, etc.).
         * @return Vulkan pipeline for the given key.
         */
        [[nodiscard]] vk::Pipeline getPrimitiveRenderer(const PrimitivePipelineKey &key) const {
            if (auto it = primitivePipelines.find(key); it != primitivePipelines.end()) {
                return it->second;
            }
            else if (key.unlit) {
                return primitivePipelines.try_emplace(it, key, createUnlitPrimitiveRenderer(
                    gpu.device, primitivePipelineLayout, sceneRenderPass, key.alphaMode))->second;
            }
            else {
                return primitivePipelines.try_emplace(it, key, createPrimitiveRenderer(
                    gpu.device, primitivePipelineLayout, sceneRenderPass, key.texcoordCount, key.fragmentShaderGeneratedTBN, key.alphaMode))->second;
            }
        }

        /**
         * @brief Get corresponding Vulkan pipeline for depth prepass rendering.
         * @param mask Boolean whether the rendering primitive's material alpha mode is MASK or not.
         * @return Vulkan pipeline for depth prepass rendering.
         */
        [[nodiscard]] vk::Pipeline getDepthPrepassRenderer(bool mask) const {
            std::optional<vk::raii::Pipeline> &targetPipeline = mask ? maskDepthRenderer : depthRenderer;
            if (!targetPipeline) {
                targetPipeline = createDepthRenderer(gpu.device, primitiveNoShadingPipelineLayout, mask);
            }
            return *targetPipeline;
        }

        /**
         * @brief Get corresponding Vulkan pipeline for jump flood seeding.
         * @param mask Boolean whether the rendering primitive's material alpha mode is MASK or not.
         * @return Vulkan pipeline for jump flood seeding.
         */
        [[nodiscard]] vk::Pipeline getJumpFloodSeedRenderer(bool mask) const {
            std::optional<vk::raii::Pipeline> &targetPipeline = mask ? maskJumpFloodSeedRenderer : jumpFloodSeedRenderer;
            if (!targetPipeline) {
                targetPipeline = createJumpFloodSeedRenderer(gpu.device, primitiveNoShadingPipelineLayout, mask);
            }
            return *targetPipeline;
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

        void updateTextureCount(std::uint32_t textureCount) {
            if (assetDescriptorSetLayout.descriptorCounts[2] == textureCount) {
                // If texture count is same, descriptor set layouts, pipeline layouts and pipelines doesn't have to be recreated.
                return;
            }

            // Following pipelines are dependent to the assetDescriptorSetLayout.
            primitivePipelines.clear();
            depthRenderer.reset();
            jumpFloodSeedRenderer.reset();
            maskDepthRenderer.reset();
            maskJumpFloodSeedRenderer.reset();

            assetDescriptorSetLayout = { gpu.device, textureCount };
            primitivePipelineLayout = { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout, sceneDescriptorSetLayout) };
            primitiveNoShadingPipelineLayout = { gpu.device, std::tie(assetDescriptorSetLayout, sceneDescriptorSetLayout) };

            textureDescriptorPool = createTextureDescriptorPool();
            std::tie(assetDescriptorSet) = vku::allocateDescriptorSets(*gpu.device, *textureDescriptorPool, std::tie(assetDescriptorSetLayout));
        }

    private:
        // --------------------
        // Pipelines.
        // --------------------

        // glTF primitive rendering pipelines.
        mutable std::unordered_map<PrimitivePipelineKey, vk::raii::Pipeline, AggregateHasher<PrimitivePipelineKey>> primitivePipelines;
        mutable std::optional<vk::raii::Pipeline> depthRenderer;
        mutable std::optional<vk::raii::Pipeline> jumpFloodSeedRenderer;
        mutable std::optional<vk::raii::Pipeline> maskDepthRenderer;
        mutable std::optional<vk::raii::Pipeline> maskJumpFloodSeedRenderer;

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
