module;

#include <cassert>

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.Frame;

import std;
export import :gltf.NodeWorldTransforms;
export import :gltf.OrderedPrimitives;
import :helpers.optional;
export import :math.Frustum;
import :vulkan.ag.DepthPrepass;
import :vulkan.ag.JumpFloodSeed;
import :vulkan.ag.SceneOpaque;
import :vulkan.ag.SceneWeightedBlended;
import :vulkan.buffer.IndirectDrawCommands;
import :vulkan.buffer.InstancedNodeWorldTransforms;
import :vulkan.buffer.MorphTargetWeights;
import :vulkan.buffer.Nodes;
export import :vulkan.SharedData;

/**
 * @brief A type that represents the state for a single multi-draw-indirect call.
 *
 * Multi-draw-indirect call execute multiple draw calls with single state. Therefore, if there are many draw calls whose
 * PSOs are different, they need to be grouped by each draw call's required state, and the grouped call can be executed
 * with a single multi-draw-indirect call.
 *
 * This type will be used for the key of associated grouped draw calls.
 */
struct CommandSeparationCriteria {
    std::uint32_t subpass;
    vk::Pipeline pipeline;
    std::optional<vk::IndexType> indexType;
    vk::PrimitiveTopology primitiveTopology;
    vk::CullModeFlagBits cullMode;

    [[nodiscard]] std::strong_ordering operator<=>(const CommandSeparationCriteria&) const noexcept = default;
};

template <>
struct std::less<CommandSeparationCriteria> {
    using is_transparent = void;

    [[nodiscard]] bool operator()(const CommandSeparationCriteria &lhs, const CommandSeparationCriteria &rhs) const noexcept { return lhs < rhs; }
    [[nodiscard]] bool operator()(const CommandSeparationCriteria &lhs, std::uint32_t rhs) const noexcept { return lhs.subpass < rhs; }
    [[nodiscard]] bool operator()(std::uint32_t lhs, const CommandSeparationCriteria &rhs) const noexcept { return lhs < rhs.subpass; }
};

struct CommandSeparationCriteriaNoShading {
    vk::Pipeline pipeline;
    std::optional<vk::IndexType> indexType;
    vk::PrimitiveTopology primitiveTopology;
    vk::CullModeFlagBits cullMode;

    [[nodiscard]] std::strong_ordering operator<=>(const CommandSeparationCriteriaNoShading&) const noexcept = default;
};

namespace vk_gltf_viewer::vulkan {
    export class Frame {
        const SharedData &sharedData;

    public:
        struct GltfAsset {
            buffer::Nodes nodeBuffer;
            buffer::InstancedNodeWorldTransforms instancedNodeWorldTransformBuffer;
            std::optional<buffer::MorphTargetWeights> morphTargetWeightBuffer;

            // Used only if GPU does not support variable descriptor count.
            std::optional<vk::raii::DescriptorPool> descriptorPool;

            template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
            GltfAsset(
                const fastgltf::Asset &asset LIFETIMEBOUND,
                const gltf::NodeWorldTransforms &nodeWorldTransforms,
                const SharedData &sharedData LIFETIMEBOUND,
                const BufferDataAdapter &adapter = {}
            ) : nodeBuffer { asset, nodeWorldTransforms, sharedData.gltfAsset->nodeInstanceCountExclusiveScanWithCount, sharedData.gltfAsset->targetWeightCountExclusiveScanWithCount, sharedData.gltfAsset->skinJointCountExclusiveScanWithCount, sharedData.gpu.allocator },
                instancedNodeWorldTransformBuffer { asset, asset.scenes[asset.defaultScene.value_or(0)], sharedData.gltfAsset->nodeInstanceCountExclusiveScanWithCount, nodeWorldTransforms, sharedData.gpu.allocator, adapter },
                morphTargetWeightBuffer { value_if(sharedData.gltfAsset->targetWeightCountExclusiveScanWithCount.back() != 0, [&]() {
                    return buffer::MorphTargetWeights { asset, sharedData.gltfAsset->targetWeightCountExclusiveScanWithCount, sharedData.gpu };
                }) },
                descriptorPool { value_if(!sharedData.gpu.supportVariableDescriptorCount, [&]() {
                    return vk::raii::DescriptorPool {
                        sharedData.gpu.device,
                        sharedData.assetDescriptorSetLayout.getPoolSize()
                            .getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind),
                    };
                }) } { }
        };

        struct ExecutionTask {
            struct Gltf {
                struct RenderingNodes {
                    std::unordered_set<std::uint16_t> indices;
                };

                struct HoveringNode {
                    std::uint16_t index;
                    glm::vec4 outlineColor;
                    float outlineThickness;
                };

                struct SelectedNodes {
                    const std::unordered_set<std::uint16_t>& indices;
                    glm::vec4 outlineColor;
                    float outlineThickness;
                };

                const fastgltf::Asset &asset;
                const gltf::OrderedPrimitives &orderedPrimitives;
                const gltf::NodeWorldTransforms &nodeWorldTransforms;

                bool regenerateDrawCommands;
                RenderingNodes renderingNodes;
                std::optional<HoveringNode> hoveringNode;
                std::optional<SelectedNodes> selectedNodes;
            };

            vk::Rect2D passthruRect;

            /**
             * @brief Camera matrices.
             */
            struct { glm::mat4 view, projection; } camera;

            /**
             * @brief The frustum of the camera, which would be used for frustum culling.
             *
             * If <tt>std::nullopt</tt>, frustum culling would be disabled.
             */
            std::optional<math::Frustum> frustum;

            /**
             * @brief Cursor position from passthru rect's top left. <tt>std::nullopt</tt> if cursor is outside the passthru rect.
             */
            std::optional<vk::Offset2D> cursorPosFromPassthruRectTopLeft;

            /**
             * @brief Information of glTF to be rendered. <tt>std::nullopt</tt> if no glTF scene to be rendered.
             */
            std::optional<Gltf> gltf;
            std::optional<glm::vec3> solidBackground; // If this is nullopt, use SharedData::SkyboxDescriptorSet instead.

            /**
             * @brief Indication whether the frame should handle swapchain resizing.
             *
             * This MUST be <tt>true</tt> if previous frame's execution result (obtained by <tt>Frame::execute()</tt>) is <tt>false</tt>.
             */
            bool handleSwapchainResize;
        };

        struct UpdateResult {
            /**
             * @brief Node index of the current pointing mesh. <tt>std::nullopt</tt> if there is no mesh under the cursor.
             */
            std::optional<std::uint16_t> hoveringNodeIndex;
        };

        // --------------------
        // glTF asset.
        // --------------------

        std::optional<GltfAsset> gltfAsset;
        vku::DescriptorSet<dsl::Asset> assetDescriptorSet;

        explicit Frame(const SharedData &sharedData LIFETIMEBOUND);

        /**
         * @brief Wait for the previous frame execution to finish.
         *
         * This function is blocking.
         * You should call this function before mutating the frame GPU resources for avoiding synchronization error.
         */
        void waitForPreviousExecution() const {
            std::ignore = sharedData.gpu.device.waitForFences(*inFlightFence, true, ~0ULL); // TODO: failure handling
            sharedData.gpu.device.resetFences(*inFlightFence);
        }

        UpdateResult update(const ExecutionTask &task);

        void recordCommandsAndSubmit(std::uint32_t swapchainImageIndex) const;

        /**
         * @brief Frame exclusive semaphore that have to be signaled when the swapchain image is acquired.
         * @return The semaphore.
         */
        [[nodiscard]] vk::Semaphore getSwapchainImageAcquireSemaphore() const noexcept { return *swapchainImageAcquireSema; }

        /**
         * @brief Frame exclusive semaphore that will to be signaled when the swapchain image is rendered and ready to be presented.
         * @return The semaphore.
         */
        [[nodiscard]] vk::Semaphore getSwapchainImageReadySemaphore() const noexcept { return *compositionFinishSema; }

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void changeAsset(
            const fastgltf::Asset &asset,
            const gltf::NodeWorldTransforms &nodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            const auto &inner = gltfAsset.emplace(asset, nodeWorldTransforms, sharedData, adapter);
            if (sharedData.gpu.supportVariableDescriptorCount) {
                (*sharedData.gpu.device).freeDescriptorSets(*descriptorPool, assetDescriptorSet);
                assetDescriptorSet = decltype(assetDescriptorSet) {
                    vku::unsafe,
                    (*sharedData.gpu.device).allocateDescriptorSets(vk::StructureChain {
                         vk::DescriptorSetAllocateInfo {
                             *descriptorPool,
                             *sharedData.assetDescriptorSetLayout,
                         },
                         vk::DescriptorSetVariableDescriptorCountAllocateInfo {
                             vku::unsafeProxy<std::uint32_t>(asset.textures.size() + 1),
                         },
                     }.get())[0],
                };
            }
            else {
                std::tie(assetDescriptorSet) = vku::allocateDescriptorSets(*inner.descriptorPool.value(), std::tie(sharedData.assetDescriptorSetLayout));
            }

            std::vector<vk::DescriptorImageInfo> imageInfos;
            imageInfos.reserve(asset.textures.size() + 1);
            imageInfos.emplace_back(*sharedData.fallbackTexture.sampler, *sharedData.fallbackTexture.imageView, vk::ImageLayout::eShaderReadOnlyOptimal);
            imageInfos.append_range(sharedData.gltfAsset->textures.descriptorInfos);

            boost::container::static_vector<vk::WriteDescriptorSet, dsl::Asset::bindingCount> descriptorWrites {
                assetDescriptorSet.getWrite<0>(sharedData.gltfAsset->primitiveBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<1>(inner.nodeBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<2>(inner.instancedNodeWorldTransformBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<6>(sharedData.gltfAsset->materialBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<7>(imageInfos),
            };
            if (inner.morphTargetWeightBuffer) {
                descriptorWrites.push_back(assetDescriptorSet.getWrite<3>(inner.morphTargetWeightBuffer->getDescriptorInfo()));
            }
            if (sharedData.gltfAsset->skinJointIndexAndInverseBindMatrixBuffer) {
                const auto &[skinJointIndexBuffer, inverseBindMatrixBuffer] = *sharedData.gltfAsset->skinJointIndexAndInverseBindMatrixBuffer;
                descriptorWrites.push_back(assetDescriptorSet.getWrite<4>(skinJointIndexBuffer.getDescriptorInfo()));
                descriptorWrites.push_back(assetDescriptorSet.getWrite<5>(inverseBindMatrixBuffer.getDescriptorInfo()));
            }

            sharedData.gpu.device.updateDescriptorSets(descriptorWrites, {});
        }

    private:
        class PassthruResources {
        public:
            struct JumpFloodResources {
                vku::AllocatedImage image;
                vk::raii::ImageView imageView;
                vk::raii::ImageView pingImageView;
                vk::raii::ImageView pongImageView;

                JumpFloodResources(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent);
            };

            vk::Extent2D extent;

            JumpFloodResources hoveringNodeOutlineJumpFloodResources;
            JumpFloodResources selectedNodeOutlineJumpFloodResources;

            // Attachment groups.
            ag::DepthPrepass depthPrepassAttachmentGroup;
            ag::JumpFloodSeed hoveringNodeJumpFloodSeedAttachmentGroup;
            ag::JumpFloodSeed selectedNodeJumpFloodSeedAttachmentGroup;

            PassthruResources(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent, vk::CommandBuffer graphicsCommandBuffer);

        private:
            void recordInitialImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const;
        };

        struct RenderingNodes {
            std::unordered_set<std::uint16_t> indices;
            std::map<CommandSeparationCriteria, buffer::IndirectDrawCommands> indirectDrawCommandBuffers;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> depthPrepassIndirectDrawCommandBuffers;
        };

        struct SelectedNodes {
            std::unordered_set<std::uint16_t> indices;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> jumpFloodSeedIndirectDrawCommandBuffers;
            glm::vec4 outlineColor;
            float outlineThickness;
        };

        struct HoveringNode {
            std::uint16_t index;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> jumpFloodSeedIndirectDrawCommandBuffers;
            glm::vec4 outlineColor;
            float outlineThickness;
        };

        // Buffer, image and image views.
        vku::MappedBuffer hoveringNodeIndexBuffer;
        std::optional<PassthruResources> passthruResources;

        // Attachment groups.
        ag::SceneOpaque sceneOpaqueAttachmentGroup;
        ag::SceneWeightedBlended sceneWeightedBlendedAttachmentGroup;

        // Framebuffers.
        std::vector<vk::raii::Framebuffer> framebuffers;

        // Descriptor/command pools.
        vk::raii::DescriptorPool descriptorPool;
        vk::raii::CommandPool computeCommandPool;
        vk::raii::CommandPool graphicsCommandPool;

        // Descriptor sets.
        vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> hoveringNodeJumpFloodSet;
        vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> selectedNodeJumpFloodSet;
        vku::DescriptorSet<OutlineRenderer::DescriptorSetLayout> hoveringNodeOutlineSet;
        vku::DescriptorSet<OutlineRenderer::DescriptorSetLayout> selectedNodeOutlineSet;
        vku::DescriptorSet<WeightedBlendedCompositionRenderer::DescriptorSetLayout> weightedBlendedCompositionSet;

        // Command buffers.
        vk::CommandBuffer scenePrepassCommandBuffer;
        vk::CommandBuffer sceneRenderingCommandBuffer;
        vk::CommandBuffer compositionCommandBuffer;
        vk::CommandBuffer jumpFloodCommandBuffer;

        // Synchronization stuffs.
        vk::raii::Semaphore scenePrepassFinishSema;
        vk::raii::Semaphore swapchainImageAcquireSema;
        vk::raii::Semaphore sceneRenderingFinishSema;
        vk::raii::Semaphore compositionFinishSema;
        vk::raii::Semaphore jumpFloodFinishSema;
        vk::raii::Fence inFlightFence;

        vk::Rect2D passthruRect;
        glm::mat4 projectionViewMatrix;
        glm::vec3 viewPosition;
        glm::mat4 translationlessProjectionViewMatrix;
        std::optional<vk::Offset2D> cursorPosFromPassthruRectTopLeft;
        std::optional<RenderingNodes> renderingNodes;
        std::optional<SelectedNodes> selectedNodes;
        std::optional<HoveringNode> hoveringNode;
        std::variant<vku::DescriptorSet<dsl::Skybox>, glm::vec3> background;

        [[nodiscard]] std::vector<vk::raii::Framebuffer> createFramebuffers() const;
        [[nodiscard]] vk::raii::DescriptorPool createDescriptorPool() const;

        void recordScenePrepassCommands(vk::CommandBuffer cb) const;
        // Return true if last jump flood calculation direction is forward (result is in pong image), false if backward.
        [[nodiscard]] bool recordJumpFloodComputeCommands(vk::CommandBuffer cb, const vku::Image &image, vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSet, std::uint32_t initialSampleOffset) const;
        void recordSceneOpaqueMeshDrawCommands(vk::CommandBuffer cb) const;
        bool recordSceneBlendMeshDrawCommands(vk::CommandBuffer cb) const;
        void recordSkyboxDrawCommands(vk::CommandBuffer cb) const;
        void recordNodeOutlineCompositionCommands(vk::CommandBuffer cb, std::optional<bool> hoveringNodeJumpFloodForward, std::optional<bool> selectedNodeJumpFloodForward, std::uint32_t swapchainImageIndex) const;
        void recordImGuiCompositionCommands(vk::CommandBuffer cb, std::uint32_t swapchainImageIndex) const;

        void recordSwapchainExtentDependentImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const;
    };
}