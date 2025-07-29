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

            // Used only if GPU does not support variable descriptor count.
            std::optional<vk::raii::DescriptorPool> descriptorPool;

            std::variant<std::monostate, vk::Offset2D, vk::Rect2D> mousePickingInput;

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
                 * @brief Cursor position or selection rectangle for handling mouse picking.
                 *
                 * - If mouse picking has to be done inside the selection rectangle, passthrough rectangle aligned, framebuffer-scale <tt>vk::Rect2D</tt> used.
                 * - If mouse picking has to be done under the current cursor, passthrough rectangle aligned <tt>vk::Offset2D</tt> used.
                 * - Otherwise, <tt>std::monostate</tt> used.
                 */
                std::variant<std::monostate, vk::Offset2D, vk::Rect2D> mousePickingInput;
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

        void setPassthruExtent(const vk::Extent2D &extent);

        void updateAsset();

    private:
        struct PassthruResources {
            struct JumpFloodResources {
                vku::AllocatedImage image;
                vk::raii::ImageView imageView;
                std::array<vk::raii::ImageView, 2> pingPongImageViews;

                JumpFloodResources(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent);
            };

            vk::Extent2D extent;

            // Mouse picking.
            ag::MousePicking mousePickingAttachmentGroup;

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

            PassthruResources(const SharedData &sharedData LIFETIMEBOUND, const vk::Extent2D &extent, vk::CommandBuffer graphicsCommandBuffer);
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
        vku::AllocatedBuffer translationlessCameraBuffer;
        std::optional<PassthruResources> passthruResources;

        // Descriptor/command pools.
        vk::raii::DescriptorPool descriptorPool;
        vk::raii::CommandPool computeCommandPool;
        vk::raii::CommandPool graphicsCommandPool;

        // Descriptor sets.
    public: // TODO
        vku::DescriptorSet<dsl::Renderer> rendererSet;
        vku::DescriptorSet<dsl::MousePicking> mousePickingSet;
        vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> hoveringNodeJumpFloodSet;
        vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> selectedNodeJumpFloodSet;
        vku::DescriptorSet<OutlineRenderer::DescriptorSetLayout> hoveringNodeOutlineSet;
        vku::DescriptorSet<OutlineRenderer::DescriptorSetLayout> selectedNodeOutlineSet;
        vku::DescriptorSet<SkyboxRenderer::DescriptorSetLayout> skyboxSet;
        vku::DescriptorSet<WeightedBlendedCompositionRenderer::DescriptorSetLayout> weightedBlendedCompositionSet;
        vku::DescriptorSet<InverseToneMappingRenderer::DescriptorSetLayout> inverseToneMappingSet;
        vku::DescriptorSet<bloom::BloomComputer::DescriptorSetLayout> bloomSet;
        vku::DescriptorSet<BloomApplyRenderer::DescriptorSetLayout> bloomApplySet;

    private: // TODO
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

        [[nodiscard]] vk::raii::DescriptorPool createDescriptorPool() const;

        void recordScenePrepassCommands(vk::CommandBuffer cb) const;
        // Return true if last jump flood calculation direction is forward (result is in pong image), false if backward.
        [[nodiscard]] bool recordJumpFloodComputeCommands(vk::CommandBuffer cb, const vku::Image &image, vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSet, std::uint32_t initialSampleOffset) const;
        void recordSceneOpaqueMeshDrawCommands(vk::CommandBuffer cb) const;
        bool recordSceneBlendMeshDrawCommands(vk::CommandBuffer cb) const;
        void recordNodeOutlineCompositionCommands(vk::CommandBuffer cb, std::optional<bool> hoveringNodeJumpFloodForward, std::optional<bool> selectedNodeJumpFloodForward) const;
        void recordImGuiCompositionCommands(vk::CommandBuffer cb, std::uint32_t swapchainImageIndex) const;
    };
}