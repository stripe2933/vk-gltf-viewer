module;

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.SharedData;

import std;
export import fastgltf;
import imgui.vulkan;
export import vku;
export import :gltf.OrderedPrimitives;
export import :gltf.data_structure.NodeInstanceCountExclusiveScanWithCount;
export import :gltf.data_structure.TargetWeightCountExclusiveScanWithCount;
import :helpers.AggregateHasher;
import :helpers.fastgltf;
import :helpers.optional;
import :helpers.ranges;
export import :vulkan.ag.Swapchain;
export import :vulkan.buffer.CombinedIndices;
export import :vulkan.buffer.InverseBindMatrices;
export import :vulkan.buffer.Materials;
export import :vulkan.buffer.PrimitiveAttributes;
export import :vulkan.buffer.Primitives;
export import :vulkan.buffer.SkinJointIndices;
import :vulkan.buffer.StagingBufferStorage;
export import :vulkan.Gpu;
export import :vulkan.pipeline.JumpFloodComputer;
export import :vulkan.pipeline.JumpFloodSeedRenderer;
export import :vulkan.pipeline.MousePickingRenderer;
export import :vulkan.pipeline.MultiNodeMousePickingRenderer;
export import :vulkan.pipeline.NodeIndexRenderer;
export import :vulkan.pipeline.OutlineRenderer;
export import :vulkan.pipeline.PrimitiveRenderer;
export import :vulkan.pipeline.SkyboxRenderer;
export import :vulkan.pipeline.UnlitPrimitiveRenderer;
export import :vulkan.pipeline.WeightedBlendedCompositionRenderer;
export import :vulkan.rp.MousePicking;
export import :vulkan.rp.Scene;
export import :vulkan.sampler.Samplers;
export import :vulkan.texture.Fallback;
export import :vulkan.texture.ImGuiColorSpaceAndUsageCorrectedTextures;
export import :vulkan.texture.Textures;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
    public:
        struct GltfAsset {
            gltf::ds::NodeInstanceCountExclusiveScanWithCount nodeInstanceCountExclusiveScanWithCount;
            gltf::ds::TargetWeightCountExclusiveScanWithCount targetWeightCountExclusiveScanWithCount;
            gltf::ds::SkinJointCountExclusiveScanWithCount skinJointCountExclusiveScanWithCount;

            buffer::Materials materialBuffer;
            buffer::CombinedIndices combinedIndexBuffers;
            buffer::PrimitiveAttributes primitiveAttributes;
            buffer::Primitives primitiveBuffer;
            std::optional<std::pair<buffer::SkinJointIndices, buffer::InverseBindMatrices>> skinJointIndexAndInverseBindMatrixBuffer;
            texture::Textures textures;
            texture::ImGuiColorSpaceAndUsageCorrectedTextures imGuiColorSpaceAndUsageCorrectedTextures;

            std::vector<vk::DescriptorSet> imGuiTextureDescriptorSets;

            template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
            GltfAsset(
                const fastgltf::Asset &asset,
                const std::filesystem::path &directory,
                const gltf::OrderedPrimitives &orderedPrimitives,
                const Gpu &gpu,
                const texture::Fallback &fallbackTexture,
                const BufferDataAdapter &adapter = {},
                buffer::StagingBufferStorage stagingBufferStorage = {},
                BS::thread_pool<> threadPool = {}
            ) : nodeInstanceCountExclusiveScanWithCount { asset },
                targetWeightCountExclusiveScanWithCount { asset },
                skinJointCountExclusiveScanWithCount { asset },
                materialBuffer { asset, gpu.allocator, stagingBufferStorage },
                combinedIndexBuffers { asset, gpu, stagingBufferStorage, adapter },
                primitiveAttributes { asset, gpu, stagingBufferStorage, threadPool, adapter },
                primitiveBuffer { orderedPrimitives, primitiveAttributes, gpu, stagingBufferStorage },
                skinJointIndexAndInverseBindMatrixBuffer { value_if(!asset.skins.empty(), [&]() {
                    return std::pair<buffer::SkinJointIndices, buffer::InverseBindMatrices> {
                        std::piecewise_construct,
                        std::tie(asset, skinJointCountExclusiveScanWithCount, gpu.allocator, stagingBufferStorage),
                        std::tie(asset, gpu.allocator, stagingBufferStorage, adapter),
                    };
                }) },
                textures { asset, directory, gpu, fallbackTexture, threadPool, adapter },
                imGuiColorSpaceAndUsageCorrectedTextures { asset, textures, gpu } {
                if (stagingBufferStorage.hasStagingCommands()) {
                    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
                    const vk::raii::Fence transferFence { gpu.device, vk::FenceCreateInfo{} };
                    vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
                        stagingBufferStorage.recordStagingCommands(cb);
                    }, *transferFence);
                    std::ignore = gpu.device.waitForFences(*transferFence, true, ~0ULL); // TODO: failure handling
                }

                imGuiTextureDescriptorSets
                    = textures.descriptorInfos
                    | std::views::transform([](const vk::DescriptorImageInfo &descriptorInfo) -> vk::DescriptorSet {
                        return ImGui_ImplVulkan_AddTexture(descriptorInfo.sampler, descriptorInfo.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    })
                    | std::ranges::to<std::vector>();
            }

            ~GltfAsset() {
                std::ranges::for_each(imGuiTextureDescriptorSets, ImGui_ImplVulkan_RemoveTexture);
            }
        };

        const Gpu &gpu;

        // --------------------
        // Non-owning swapchain resources.
        // --------------------

        vk::Extent2D swapchainExtent;
        std::span<const vk::Image> swapchainImages;

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

        // Pipeline layouts.
        pl::MultiNodeMousePicking multiNodeMousePickingPipelineLayout;
        pl::Primitive primitivePipelineLayout;
        pl::PrimitiveNoShading primitiveNoShadingPipelineLayout;

        // --------------------
        // Pipelines.
        // --------------------

        // Primitive unrelated pipelines.
        JumpFloodComputer jumpFloodComputer;
        MousePickingRenderer mousePickingRenderer;
        OutlineRenderer outlineRenderer;
        SkyboxRenderer skyboxRenderer;
        WeightedBlendedCompositionRenderer weightedBlendedCompositionRenderer;

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
        std::optional<GltfAsset> gltfAsset;

        SharedData(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &swapchainExtent, std::span<const vk::Image> swapchainImages)
            : gpu { gpu }
            , swapchainExtent { swapchainExtent }
            , swapchainImages { swapchainImages }
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
            , sceneRenderPass { gpu.device }
            , multiNodeMousePickingPipelineLayout { gpu.device, std::tie(assetDescriptorSetLayout, multiNodeMousePickingDescriptorSetLayout) }
            , primitivePipelineLayout { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout) }
            , primitiveNoShadingPipelineLayout { gpu.device, assetDescriptorSetLayout }
            , jumpFloodComputer { gpu.device }
            , mousePickingRenderer { gpu.device, mousePickingRenderPass }
            , outlineRenderer { gpu.device }
            , skyboxRenderer { gpu.device, skyboxDescriptorSetLayout, true, sceneRenderPass, cubeIndices }
            , weightedBlendedCompositionRenderer { gpu.device, sceneRenderPass }
            , imGuiAttachmentGroup { gpu, swapchainExtent, swapchainImages }
            , descriptorPool { gpu.device, getPoolSizes(imageBasedLightingDescriptorSetLayout, skyboxDescriptorSetLayout).getDescriptorPoolCreateInfo() }
            , fallbackTexture { gpu }{
            std::tie(imageBasedLightingDescriptorSet, skyboxDescriptorSet) = vku::allocateDescriptorSets(
                *descriptorPool, std::tie(imageBasedLightingDescriptorSetLayout, skyboxDescriptorSetLayout));
        }

        // --------------------
        // Pipeline selectors.
        // --------------------

        [[nodiscard]] vk::Pipeline getNodeIndexRenderer(const NodeIndexRendererSpecialization &specialization) const {
            return ranges::try_emplace_if_not_exists(nodeIndexPipelines, specialization, [&]() {
                return specialization.createPipeline(gpu.device, primitiveNoShadingPipelineLayout, mousePickingRenderPass);
            }).first->second;
        }

        [[nodiscard]] vk::Pipeline getMaskNodeIndexRenderer(const MaskNodeIndexRendererSpecialization &specialization) const {
            return ranges::try_emplace_if_not_exists(maskNodeIndexPipelines, specialization, [&]() {
                return specialization.createPipeline(gpu.device, primitiveNoShadingPipelineLayout, mousePickingRenderPass);
            }).first->second;
        }

        [[nodiscard]] vk::Pipeline getMultiNodeMousePickingRenderer(const MultiNodeMousePickingRendererSpecialization &specialization) const {
            return ranges::try_emplace_if_not_exists(multiNodeMousePickingPipelines, specialization, [&]() {
                return specialization.createPipeline(gpu, multiNodeMousePickingPipelineLayout);
            }).first->second;
        }

        [[nodiscard]] vk::Pipeline getMaskMultiNodeMousePickingRenderer(const MaskMultiNodeMousePickingRendererSpecialization &specialization) const {
            return ranges::try_emplace_if_not_exists(maskMultiNodeMousePickingPipelines, specialization, [&]() {
                return specialization.createPipeline(gpu, multiNodeMousePickingPipelineLayout);
            }).first->second;
        }

        [[nodiscard]] vk::Pipeline getJumpFloodSeedRenderer(const JumpFloodSeedRendererSpecialization &specialization) const {
            return ranges::try_emplace_if_not_exists(jumpFloodSeedPipelines, specialization, [&]() {
                return specialization.createPipeline(gpu.device, primitiveNoShadingPipelineLayout);
            }).first->second;
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

            imGuiAttachmentGroup = { gpu, swapchainExtent, swapchainImages };
        }

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void changeAsset(
            const fastgltf::Asset &asset,
            const std::filesystem::path &directory,
            const gltf::OrderedPrimitives &orderedPrimitives,
            const BufferDataAdapter &adapter = {}
        ) {
            // If asset texture count exceeds the available texture count provided by the GPU, throw the error before
            // processing data to avoid unnecessary processing.
            const std::uint32_t textureCount = 1 + asset.textures.size();
            if (textureCount > dsl::Asset::maxTextureCount(gpu)) {
                throw gltf::AssetProcessError::TooManyTextureError;
            }

            gltfAsset.emplace(asset, directory, orderedPrimitives, gpu, fallbackTexture, adapter);
            if (!gpu.supportVariableDescriptorCount && assetDescriptorSetLayout.descriptorCounts[6] != textureCount) {
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

    private:
        // --------------------
        // Pipelines.
        // --------------------

        // glTF primitive rendering pipelines.
        mutable std::unordered_map<NodeIndexRendererSpecialization, vk::raii::Pipeline, AggregateHasher> nodeIndexPipelines;
        mutable std::unordered_map<MaskNodeIndexRendererSpecialization, vk::raii::Pipeline, AggregateHasher> maskNodeIndexPipelines;
        mutable std::unordered_map<MultiNodeMousePickingRendererSpecialization, vk::raii::Pipeline, AggregateHasher> multiNodeMousePickingPipelines;
        mutable std::unordered_map<MaskMultiNodeMousePickingRendererSpecialization, vk::raii::Pipeline, AggregateHasher> maskMultiNodeMousePickingPipelines;
        mutable std::unordered_map<JumpFloodSeedRendererSpecialization, vk::raii::Pipeline, AggregateHasher> jumpFloodSeedPipelines;
        mutable std::unordered_map<MaskJumpFloodSeedRendererSpecialization, vk::raii::Pipeline, AggregateHasher> maskJumpFloodSeedPipelines;
        mutable std::unordered_map<PrimitiveRendererSpecialization, vk::raii::Pipeline, AggregateHasher> primitivePipelines;
        mutable std::unordered_map<UnlitPrimitiveRendererSpecialization, vk::raii::Pipeline, AggregateHasher> unlitPrimitivePipelines;
    };
}
