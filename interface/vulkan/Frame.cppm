module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.Frame;

import std;
export import :gltf.AssetGpuBuffers;
export import :gltf.AssetSceneGpuBuffers;
export import :math.Frustum;
export import :vulkan.SharedData;
import :vulkan.ag.DepthPrepass;
import :vulkan.ag.JumpFloodSeed;
import :vulkan.ag.SceneOpaque;
import :vulkan.ag.SceneWeightedBlended;

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
    std::optional<std::pair<vk::Buffer, vk::IndexType>> indexBufferAndType;
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
    std::optional<std::pair<vk::Buffer, vk::IndexType>> indexBufferAndType;
    vk::CullModeFlagBits cullMode;

    [[nodiscard]] std::strong_ordering operator<=>(const CommandSeparationCriteriaNoShading&) const noexcept = default;
};

namespace vk_gltf_viewer::vulkan {
    export class Frame {
    public:
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
                const gltf::AssetGpuBuffers &assetGpuBuffers;
                const gltf::AssetSceneHierarchy &sceneHierarchy;
                const gltf::AssetSceneGpuBuffers &sceneGpuBuffers;

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

        Frame(const Gpu &gpu [[clang::lifetimebound]], const SharedData &sharedData [[clang::lifetimebound]]);

        /**
         * @brief Wait for the previous frame execution to finish.
         *
         * This function is blocking.
         * You should call this function before mutating the frame GPU resources for avoiding synchronization error.
         */
        void waitForPreviousExecution() const {
            std::ignore = gpu.device.waitForFences(*inFlightFence, true, ~0ULL); // TODO: failure handling
            gpu.device.resetFences(*inFlightFence);
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

    private:
        template <typename Criteria>
        using CriteriaSeparatedIndirectDrawCommands = std::map<Criteria, std::variant<buffer::IndirectDrawCommands<false>, buffer::IndirectDrawCommands<true>>>;

        class PassthruResources {
        public:
            struct JumpFloodResources {
                vku::AllocatedImage image;
                vk::raii::ImageView imageView;
                vk::raii::ImageView pingImageView;
                vk::raii::ImageView pongImageView;

                JumpFloodResources(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &extent);
            };

            vk::Extent2D extent;

            JumpFloodResources hoveringNodeOutlineJumpFloodResources;
            JumpFloodResources selectedNodeOutlineJumpFloodResources;

            // Attachment groups.
            ag::DepthPrepass depthPrepassAttachmentGroup;
            ag::JumpFloodSeed hoveringNodeJumpFloodSeedAttachmentGroup;
            ag::JumpFloodSeed selectedNodeJumpFloodSeedAttachmentGroup;

            PassthruResources(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &extent, vk::CommandBuffer graphicsCommandBuffer);

        private:
            auto recordInitialImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
        };

        struct RenderingNodes {
            std::unordered_set<std::uint16_t> indices;
            CriteriaSeparatedIndirectDrawCommands<CommandSeparationCriteria> indirectDrawCommandBuffers;
            CriteriaSeparatedIndirectDrawCommands<CommandSeparationCriteriaNoShading> depthPrepassIndirectDrawCommandBuffers;
        };

        struct SelectedNodes {
            std::unordered_set<std::uint16_t> indices;
            CriteriaSeparatedIndirectDrawCommands<CommandSeparationCriteriaNoShading> jumpFloodSeedIndirectDrawCommandBuffers;
            glm::vec4 outlineColor;
            float outlineThickness;
        };

        struct HoveringNode {
            std::uint16_t index;
            CriteriaSeparatedIndirectDrawCommands<CommandSeparationCriteriaNoShading> jumpFloodSeedIndirectDrawCommandBuffers;
            glm::vec4 outlineColor;
            float outlineThickness;
        };

        const Gpu &gpu;
        const SharedData &sharedData;

        // Buffer, image and image views.
        vku::MappedBuffer hoveringNodeIndexBuffer;
        std::optional<PassthruResources> passthruResources = std::nullopt;

        // Attachment groups.
        ag::SceneOpaque sceneOpaqueAttachmentGroup { gpu, sharedData.swapchainExtent, sharedData.swapchainImages };
        ag::SceneWeightedBlended sceneWeightedBlendedAttachmentGroup { gpu, sharedData.swapchainExtent, sceneOpaqueAttachmentGroup.depthStencilAttachment->image };

        // Framebuffers.
        std::vector<vk::raii::Framebuffer> framebuffers = createFramebuffers();

        // Descriptor/command pools.
        vk::raii::DescriptorPool descriptorPool = createDescriptorPool();
        vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
        vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };

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
        vk::raii::Semaphore scenePrepassFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
        vk::raii::Semaphore swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} };
        vk::raii::Semaphore sceneRenderingFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
        vk::raii::Semaphore compositionFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
        vk::raii::Semaphore jumpFloodFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
        vk::raii::Fence inFlightFence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } };

        vk::Rect2D passthruRect;
        glm::mat4 projectionViewMatrix;
        glm::vec3 viewPosition;
        glm::mat4 translationlessProjectionViewMatrix;
        std::optional<vk::Offset2D> cursorPosFromPassthruRectTopLeft;
        std::optional<RenderingNodes> renderingNodes;
        std::optional<SelectedNodes> selectedNodes;
        std::optional<HoveringNode> hoveringNode;
        std::variant<vku::DescriptorSet<dsl::Skybox>, glm::vec3> background;

        [[nodiscard]] auto createFramebuffers() const -> std::vector<vk::raii::Framebuffer>;
        [[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);

        auto recordScenePrepassCommands(vk::CommandBuffer cb) const -> void;
        // Return true if last jump flood calculation direction is forward (result is in pong image), false if backward.
        [[nodiscard]] auto recordJumpFloodComputeCommands(vk::CommandBuffer cb, const vku::Image &image, vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSet, std::uint32_t initialSampleOffset) const -> bool;
        auto recordSceneOpaqueMeshDrawCommands(vk::CommandBuffer cb) const -> void;
        auto recordSceneBlendMeshDrawCommands(vk::CommandBuffer cb) const -> bool;
        auto recordSkyboxDrawCommands(vk::CommandBuffer cb) const -> void;
        auto recordNodeOutlineCompositionCommands(vk::CommandBuffer cb, std::optional<bool> hoveringNodeJumpFloodForward, std::optional<bool> selectedNodeJumpFloodForward, std::uint32_t swapchainImageIndex) const -> void;
        auto recordImGuiCompositionCommands(vk::CommandBuffer cb, std::uint32_t swapchainImageIndex) const -> void;

        auto recordSwapchainExtentDependentImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    };
}