module;

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.SharedData;

import std;
export import fastgltf;
export import vku;
export import :gltf.OrderedPrimitives;
import :helpers.AggregateHasher;
import :helpers.fastgltf;
import :helpers.ranges;
export import :vulkan.ag.Swapchain;
import :vulkan.buffer.CombinedIndices;
import :vulkan.buffer.Materials;
import :vulkan.buffer.Nodes;
import :vulkan.buffer.PrimitiveAttributes;
import :vulkan.buffer.Primitives;
import :vulkan.buffer.StagingBufferStorage;
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
export import :vulkan.texture.Fallback;
export import :vulkan.texture.Textures;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
        const Gpu &gpu;

    public:
        struct GltfAsset {
            // --------------------
            // Buffers.
            // --------------------

            buffer::InstancedNodeWorldTransforms instancedNodeWorldTransformBuffer;
            buffer::Nodes nodeBuffer;
            buffer::Materials materialBuffer;
            buffer::CombinedIndices combinedIndexBuffers;
            buffer::PrimitiveAttributes primitiveAttributes;
            buffer::Primitives primitiveBuffer;

            // --------------------
            // Textures.
            // --------------------

            texture::Textures textures;

            template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
            GltfAsset(
                const fastgltf::Asset &asset,
                const std::filesystem::path &directory,
                const gltf::NodeWorldTransforms &nodeWorldTransforms,
                const gltf::OrderedPrimitives &orderedPrimitives,
                const Gpu &gpu,
                const BufferDataAdapter &adapter = {},
                buffer::StagingBufferStorage stagingBufferStorage = {},
                BS::thread_pool<> threadPool = {}
            ) : instancedNodeWorldTransformBuffer { asset, nodeWorldTransforms, gpu.allocator, adapter },
                nodeBuffer { asset, instancedNodeWorldTransformBuffer, gpu.allocator, stagingBufferStorage },
                materialBuffer { asset, gpu.allocator, stagingBufferStorage },
                combinedIndexBuffers { asset, gpu, stagingBufferStorage, adapter },
                primitiveAttributes { asset, gpu, stagingBufferStorage, threadPool, adapter },
                primitiveBuffer { materialBuffer, orderedPrimitives, primitiveAttributes, gpu, stagingBufferStorage },
                textures { asset, directory, gpu, threadPool, adapter } {
                // Setup node world transforms as the default scene hierarchy.
                instancedNodeWorldTransformBuffer.update(asset.scenes[asset.defaultScene.value_or(0)], nodeWorldTransforms, adapter);

                if (stagingBufferStorage.hasStagingCommands()) {
                    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
                    const vk::raii::Fence transferFence { gpu.device, vk::FenceCreateInfo{} };
                    vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
                        stagingBufferStorage.recordStagingCommands(cb);
                    }, *transferFence);
                    std::ignore = gpu.device.waitForFences(*transferFence, true, ~0ULL); // TODO: failure handling
                }
            }
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

        // Descriptor set layouts.
        dsl::Asset assetDescriptorSetLayout { gpu.device, 1 }; // TODO: set proper initial texture count.
        dsl::ImageBasedLighting imageBasedLightingDescriptorSetLayout { gpu.device, cubemapSampler, brdfLutSampler };
        dsl::Skybox skyboxDescriptorSetLayout { gpu.device, cubemapSampler };

        // Render passes.
        rp::Scene sceneRenderPass { gpu.device };

        // Pipeline layouts.
        pl::Primitive primitivePipelineLayout { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout) };
        pl::PrimitiveNoShading primitiveNoShadingPipelineLayout { gpu.device, assetDescriptorSetLayout };

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
        vku::DescriptorSet<dsl::ImageBasedLighting> imageBasedLightingDescriptorSet;
        vku::DescriptorSet<dsl::Skybox> skyboxDescriptorSet;

        // --------------------
        // glTF assets.
        // --------------------

        texture::Fallback fallbackTexture;
        std::optional<GltfAsset> gltfAsset;

        SharedData(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages)
            : gpu { gpu }
            , swapchainExtent { swapchainExtent }
            , swapchainImages { swapchainImages }
            , fallbackTexture { gpu }{
            std::tie(assetDescriptorSet)
                = vku::allocateDescriptorSets(*gpu.device, *textureDescriptorPool, std::tie(
                    assetDescriptorSetLayout));
            std::tie(imageBasedLightingDescriptorSet, skyboxDescriptorSet)
                = vku::allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(
                    imageBasedLightingDescriptorSetLayout,
                    skyboxDescriptorSetLayout));
        }

        // --------------------
        // Pipeline selectors.
        // --------------------

        [[nodiscard]] vk::Pipeline getDepthRenderer() const {
            if (!depthRenderer) {
                depthRenderer = createDepthRenderer(gpu.device, primitiveNoShadingPipelineLayout);
            }
            return *depthRenderer;
        }

        [[nodiscard]] vk::Pipeline getMaskDepthRenderer(const MaskDepthRendererSpecialization &specialization) const {
            return ranges::try_emplace_if_not_exists(maskDepthPipelines, specialization, [&]() {
                return specialization.createPipeline(gpu.device, primitiveNoShadingPipelineLayout);
            }).first->second;
        }

        [[nodiscard]] vk::Pipeline getJumpFloodSeedRenderer() const {
            if (!jumpFloodSeedRenderer) {
                jumpFloodSeedRenderer = createJumpFloodSeedRenderer(gpu.device, primitiveNoShadingPipelineLayout);
            }
            return *jumpFloodSeedRenderer;
        }

        [[nodiscard]] vk::Pipeline getMaskJumpFloodSeedRenderer(const MaskJumpFloodSeedRendererSpecialization &specialization) const {
            return ranges::try_emplace_if_not_exists(maskJumpFloodSeedPipelines, specialization, [&]() {
                return specialization.createPipeline(gpu.device, primitiveNoShadingPipelineLayout);
            }).first->second;
        }

        [[nodiscard]] vk::Pipeline getPrimitiveRenderer(const PrimitiveRendererSpecialization &specialization) const {
            return ranges::try_emplace_if_not_exists(primitivePipelines, specialization, [&]() {
                return specialization.createPipeline(gpu.device, primitivePipelineLayout, sceneRenderPass);
            }).first->second;
        }

        [[nodiscard]] vk::Pipeline getUnlitPrimitiveRenderer(const UnlitPrimitiveRendererSpecialization &specialization) const {
            return ranges::try_emplace_if_not_exists(unlitPrimitivePipelines, specialization, [&]() {
                return specialization.createPipeline(gpu.device, primitivePipelineLayout, sceneRenderPass);
            }).first->second;
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

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void changeAsset(
            const fastgltf::Asset &asset,
            const std::filesystem::path &directory,
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
            const gltf::OrderedPrimitives &orderedPrimitives,
            const BufferDataAdapter &adapter = {}
        ) {
            const GltfAsset &inner = gltfAsset.emplace(asset, directory, nodeWorldTransforms, orderedPrimitives, gpu, adapter);

            const std::uint32_t textureCount = 1 + asset.textures.size();
            if (assetDescriptorSetLayout.descriptorCounts[4] != textureCount) {
                // If texture count is different, descriptor set layouts, pipeline layouts and pipelines have to be recreated.
                depthRenderer.reset();
                maskDepthPipelines.clear();
                jumpFloodSeedRenderer.reset();
                maskJumpFloodSeedPipelines.clear();
                primitivePipelines.clear();
                unlitPrimitivePipelines.clear();

                assetDescriptorSetLayout = { gpu.device, textureCount };
                primitivePipelineLayout = { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout) };
                primitiveNoShadingPipelineLayout = { gpu.device, assetDescriptorSetLayout };

                textureDescriptorPool = createTextureDescriptorPool();
                std::tie(assetDescriptorSet) = vku::allocateDescriptorSets(*gpu.device, *textureDescriptorPool, std::tie(assetDescriptorSetLayout));
            }

            std::vector<vk::DescriptorImageInfo> imageInfos;
            imageInfos.reserve(textureCount);
            imageInfos.emplace_back(*fallbackTexture.sampler, *fallbackTexture.imageView, vk::ImageLayout::eShaderReadOnlyOptimal);
            imageInfos.append_range(asset.textures | std::views::transform([&](const fastgltf::Texture &texture) {
                return vk::DescriptorImageInfo {
                    to_optional(texture.samplerIndex)
                        .transform([&](std::size_t samplerIndex) { return *inner.textures.samplers[samplerIndex]; })
                        .value_or(*fallbackTexture.sampler),
                    *inner.textures.imageViews.at(getPreferredImageIndex(texture)),
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                };
            }));
            gpu.device.updateDescriptorSets({
                assetDescriptorSet.getWrite<0>(inner.primitiveBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<1>(inner.nodeBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<2>(inner.instancedNodeWorldTransformBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<3>(inner.materialBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<4>(imageInfos),
            }, {});
        }

    private:
        // --------------------
        // Pipelines.
        // --------------------

        // glTF primitive rendering pipelines.
        mutable std::optional<vk::raii::Pipeline> depthRenderer;
        mutable std::unordered_map<MaskDepthRendererSpecialization, vk::raii::Pipeline, AggregateHasher> maskDepthPipelines;
        mutable std::optional<vk::raii::Pipeline> jumpFloodSeedRenderer;
        mutable std::unordered_map<MaskJumpFloodSeedRendererSpecialization, vk::raii::Pipeline, AggregateHasher> maskJumpFloodSeedPipelines;
        mutable std::unordered_map<PrimitiveRendererSpecialization, vk::raii::Pipeline, AggregateHasher> primitivePipelines;
        mutable std::unordered_map<UnlitPrimitiveRendererSpecialization, vk::raii::Pipeline, AggregateHasher> unlitPrimitivePipelines;

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
            return { gpu.device, getPoolSizes(imageBasedLightingDescriptorSetLayout, skyboxDescriptorSetLayout).getDescriptorPoolCreateInfo() };
        }
    };
}
