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

namespace vk_gltf_viewer::vulkan {
    export class Frame {
    public:
        struct ExecutionTask {
            struct Gltf {
                struct RenderingNodes {
                    std::unordered_set<std::uint16_t> indices;
                    bool shouldRegenerateDrawCommands;
                };

                struct HoveringNode {
                    std::uint16_t index;
                    glm::vec4 outlineColor;
                    float outlineThickness;
                    bool shouldRegenerateDrawCommands;
                };

                struct SelectedNodes {
                    const std::unordered_set<std::uint16_t>& indices;
                    glm::vec4 outlineColor;
                    float outlineThickness;
                    bool shouldRegenerateDrawCommands;
                };

                const fastgltf::Asset &asset;
                const gltf::AssetGpuBuffers &assetGpuBuffers;
                const gltf::AssetSceneHierarchy &sceneHierarchy;
                const gltf::AssetSceneGpuBuffers &sceneGpuBuffers;
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
            if (auto result = gpu.device.waitForFences(*inFlightFence, true, ~0ULL); result != vk::Result::eSuccess) {
                throw std::runtime_error{ std::format("Failed to wait for in-flight fence: {}", to_string(result)) };
            }
            gpu.device.resetFences(*inFlightFence);
        }

        UpdateResult update(const ExecutionTask &task);

        [[nodiscard]] auto execute() const -> bool; // false -> Swapchain image acquire/release failed. Swapchain have to be recreated.

    private:
        enum class RenderingStrategy : std::uint8_t {
            Blend,
            // Note: KHR_materials_unlit is not affected by lighting, therefore per-fragment normal and tangent
            // calculation is unnecessary. Therefore, regardless whether it is faceted or not, it can be rendered with
            // the same way.
            BlendUnlit,
            BlendFaceted,
            Opaque,
            OpaqueUnlit,
            OpaqueFaceted,
            Mask,
            MaskUnlit,
            MaskFaceted,
        };

        struct CommandSeparationCriteria {
            RenderingStrategy strategy;
            std::optional<vk::IndexType> indexType;
            bool doubleSided;

            [[nodiscard]] constexpr auto operator<=>(const CommandSeparationCriteria&) const noexcept -> std::strong_ordering = default;
        };

        struct CommandSeparationCriteriaComparator {
            using is_transparent = void;

            [[nodiscard]] auto operator()(const CommandSeparationCriteria &lhs, const CommandSeparationCriteria &rhs) const noexcept -> bool { return lhs < rhs; }
            [[nodiscard]] auto operator()(const CommandSeparationCriteria &lhs, RenderingStrategy rhs) const noexcept -> bool { return lhs.strategy < rhs; }
            [[nodiscard]] auto operator()(RenderingStrategy lhs, const CommandSeparationCriteria &rhs) const noexcept -> bool { return lhs < rhs.strategy; }
        };

        using CriteriaSeparatedIndirectDrawCommands = std::map<CommandSeparationCriteria, std::variant<buffer::IndirectDrawCommands<false>, buffer::IndirectDrawCommands<true>>, CommandSeparationCriteriaComparator>;

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
            CriteriaSeparatedIndirectDrawCommands indirectDrawCommandBuffers;
        };

        struct SelectedNodes {
            std::unordered_set<std::uint16_t> indices;
            CriteriaSeparatedIndirectDrawCommands indirectDrawCommandBuffers;
            glm::vec4 outlineColor;
            float outlineThickness;
        };

        struct HoveringNode {
            std::uint16_t index;
            CriteriaSeparatedIndirectDrawCommands indirectDrawCommandBuffers;
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
        std::unordered_map<vk::IndexType, vk::Buffer> indexBuffers;
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