module;

#include <cassert>

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.Frame;

import std;
export import fastgltf;
export import glm;
export import vkgltf;
export import vku;

import vk_gltf_viewer.vulkan.ag.JumpFloodSeed;
import vk_gltf_viewer.vulkan.ag.MousePicking;
import vk_gltf_viewer.vulkan.ag.Scene;
import vk_gltf_viewer.vulkan.buffer.IndirectDrawCommands;
export import vk_gltf_viewer.Renderer;
export import vk_gltf_viewer.vulkan.SharedData;
export import vk_gltf_viewer.vulkan.Swapchain;

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
    std::optional<std::uint32_t> stencilReference;
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
            std::shared_ptr<const gltf::AssetExtended> assetExtended;

            vkgltf::NodeBuffer nodeBuffer;

            vku::MappedBuffer mousePickingResultBuffer;

            std::optional<std::pair<std::uint32_t, vk::Rect2D>> mousePickingInput;

            explicit GltfAsset(const SharedData &sharedData LIFETIMEBOUND);

            /**
             * @brief Update the node buffer's world transform data using host asset data.
             * @param nodeIndex Index of the node to be updated.
             */
            void updateNodeWorldTransform(std::size_t nodeIndex);

            /**
             * @brief Update the node buffer's world transform data using host asset data.
             * @param nodeIndex Index of the node that is used as the root of the hierarchical update.
             */
            void updateNodeWorldTransformHierarchical(std::size_t nodeIndex);

            /**
             * @brief Update the node buffer's world transform data using host asset data.
             * @param sceneIndex Index of the scene that is used to update the node world transforms.
             */
            void updateNodeWorldTransformScene(std::size_t sceneIndex);

            /**
             * @brief Update the node buffer's morph target weights using host asset data.
             * @param nodeIndex Index of the node to be updated.
             * @param startIndex Start index of the morph target weights to be updated.
             * @param count Number of morph target weights to be updated.
             */
            void updateNodeTargetWeights(std::size_t nodeIndex, std::size_t startIndex, std::size_t count);
        };

        struct ExecutionTask {
            struct Gltf {
                bool regenerateDrawCommands;

                /**
                 * @brief Selection rectangle for handling mouse picking.
                 *
                 * - If it has a value whose extent is 1x1, a node that is rasterized under the offset is selected.
                 * - If it has a value whose extent is greater than 1x1, every node that are rasterized inside the rectangle
                 *   (regardless of it its cull mode and occlusion) are selected.
                 * - If it is valueless, mouse picking is not performed.
                 *
                 * @note The rectangle must be sized, i.e. both its width and height must be greater than 0.
                 */
                std::optional<std::pair<std::uint32_t, vk::Rect2D>> mousePickingInput;
            };

            vk::Offset2D passthruOffset;

            /**
             * @brief Information of glTF to be rendered. <tt>std::nullopt</tt> if no glTF scene to be rendered.
             */
            std::optional<Gltf> gltf;
        };

        struct ExecutionResult {
            /**
             * @brief Node index of the current pointing mesh. <tt>std::nullopt</tt> if there is no mesh under the cursor.
             */
            std::variant<std::monostate, std::size_t, std::vector<std::size_t>> mousePickingResult;
        };

        std::shared_ptr<const Renderer> renderer;

        // --------------------
        // glTF asset.
        // --------------------

        std::optional<GltfAsset> gltfAsset;
        vku::DescriptorSet<dsl::Asset> assetDescriptorSet;

        Frame(std::shared_ptr<const Renderer> renderer, const SharedData &sharedData LIFETIMEBOUND);

        [[nodiscard]] ExecutionResult getExecutionResult();
        void update(const ExecutionTask &task);

        void recordCommandsAndSubmit(Swapchain &swapchain) const;

        void setViewportExtent(const vk::Extent2D &extent);

        void updateAsset();

    private:
        struct Viewport {
            struct JumpFloodResources {
                vku::AllocatedImage image;
                vk::raii::ImageView imageView;
                std::array<vk::raii::ImageView, 2> pingPongImageViews;

                JumpFloodResources(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent, std::uint32_t viewCount);
            };

            vk::Extent2D extent;

            const SharedData::ViewMaskDependentResources *shared;

            // Mouse picking.
            std::optional<ag::MousePicking> mousePickingAttachmentGroup; // has only value if Gpu::attachmentLessRenderPass == true.

            // Outline calculation using JFA.
            JumpFloodResources hoveringNodeOutlineJumpFloodResources;
            ag::JumpFloodSeed hoveringNodeJumpFloodSeedAttachmentGroup;
            JumpFloodResources selectedNodeOutlineJumpFloodResources;
            ag::JumpFloodSeed selectedNodeJumpFloodSeedAttachmentGroup;

            // Scene rendering.
            ag::Scene sceneAttachmentGroup;

            // Bloom.
            vku::AllocatedImage bloomImage;
            vk::raii::ImageView bloomImageView;
            std::vector<vk::raii::ImageView> bloomMipImageViews;

            // Framebuffers.
            vk::raii::Framebuffer sceneFramebuffer;
            vk::raii::Framebuffer bloomApplyFramebuffer;

            Viewport(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent, std::uint32_t viewCount, const SharedData::ViewMaskDependentResources &shared LIFETIMEBOUND, vk::CommandBuffer graphicsCommandBuffer);
        };

        struct RenderingNodes {
            std::map<CommandSeparationCriteria, buffer::IndirectDrawCommands> indirectDrawCommandBuffers;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> mousePickingIndirectDrawCommandBuffers;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> multiNodeMousePickingIndirectDrawCommandBuffers;
            bool startMousePickingRenderPass = true;
        };

        struct SelectedNodes {
            std::size_t indexHash; // = boost::hash_unordered_range(selectedNodes)
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> jumpFloodSeedIndirectDrawCommandBuffers;
        };

        struct HoveringNode {
            std::size_t index;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> jumpFloodSeedIndirectDrawCommandBuffers;
        };

        // Buffer, image and image views.
        vku::AllocatedBuffer cameraBuffer;
        std::optional<Viewport> viewport;

        // Descriptor/command pools.
        vk::raii::DescriptorPool descriptorPool;
        vk::raii::CommandPool computeCommandPool;
        vk::raii::CommandPool graphicsCommandPool;

        // Descriptor sets.
        vku::DescriptorSet<dsl::Renderer> rendererSet;
        vku::DescriptorSet<dsl::MousePicking> mousePickingSet;
        vku::DescriptorSet<JumpFloodComputePipeline::DescriptorSetLayout> hoveringNodeJumpFloodSet;
        vku::DescriptorSet<JumpFloodComputePipeline::DescriptorSetLayout> selectedNodeJumpFloodSet;
        vku::DescriptorSet<dsl::Outline> hoveringNodeOutlineSet;
        vku::DescriptorSet<dsl::Outline> selectedNodeOutlineSet;
        vku::DescriptorSet<dsl::WeightedBlendedComposition> weightedBlendedCompositionSet;
        vku::DescriptorSet<dsl::InverseToneMapping> inverseToneMappingSet;
        vku::DescriptorSet<bloom::BloomComputePipeline::DescriptorSetLayout> bloomSet;
        vku::DescriptorSet<dsl::BloomApply> bloomApplySet;

        // Command buffers.
        vk::CommandBuffer scenePrepassCommandBuffer;
        vk::CommandBuffer sceneRenderingCommandBuffer;
        vk::CommandBuffer compositionCommandBuffer;
        vk::CommandBuffer jumpFloodCommandBuffer;

        // Synchronization stuffs.
        vk::raii::Semaphore scenePrepassFinishSema;
        vk::raii::Semaphore sceneRenderingFinishSema;
        vk::raii::Semaphore jumpFloodFinishSema;
        vk::raii::Semaphore swapchainImageAcquireSema;
        vk::raii::Fence inFlightFence;

        vk::Offset2D passthruOffset;
        std::optional<RenderingNodes> renderingNodes;
        std::optional<SelectedNodes> selectedNodes;
        std::optional<HoveringNode> hoveringNode;
        std::variant<vku::DescriptorSet<dsl::Skybox>, glm::vec3> background;

        [[nodiscard]] vk::raii::DescriptorPool createDescriptorPool() const;

        void recordScenePrepassCommands(vk::CommandBuffer cb) const;
        // Return true if last jump flood calculation direction is forward (result is in pong image), false if backward.
        [[nodiscard]] bool recordJumpFloodComputeCommands(vk::CommandBuffer cb, const vku::Image &image, vku::DescriptorSet<JumpFloodComputePipeline::DescriptorSetLayout> descriptorSet, std::uint32_t initialSampleOffset) const;
        void recordSceneOpaqueMeshDrawCommands(vk::CommandBuffer cb) const;
        bool recordSceneBlendMeshDrawCommands(vk::CommandBuffer cb) const;
        void recordSkyboxDrawCommands(vk::CommandBuffer cb) const;
        void recordNodeOutlineCompositionCommands(vk::CommandBuffer cb, std::optional<bool> hoveringNodeJumpFloodForward, std::optional<bool> selectedNodeJumpFloodForward) const;
        void recordImGuiCompositionCommands(vk::CommandBuffer cb, std::uint32_t swapchainImageIndex) const;
    };
}