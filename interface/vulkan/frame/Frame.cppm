module;

#include <cstdint>
#include <array>
#include <compare>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <vector>

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.frame.Frame;

export import vku;
export import :vulkan.Gpu;
export import :vulkan.frame.SharedData;

namespace vk_gltf_viewer::vulkan::inline frame {
    export class Frame {
    public:
    	struct OnLoopTask {
    		vk::Rect2D passthruRect;
    		struct { glm::mat4 view, projection; } camera;
    		std::optional<vk::Offset2D> mouseCursorOffset;
    		std::optional<std::uint32_t> hoveringNodeIndex, selectedNodeIndex;
    	};

		struct OnLoopResult {
			std::optional<std::uint32_t> hoveringNodeIndex;
			bool presentSuccess;
		};

    	enum class OnLoopError {
    		SwapchainAcquireFailed,
    	};

    	Frame(const std::shared_ptr<SharedData> &sharedData, const Gpu &gpu);

    	[[nodiscard]] auto onLoop(const OnLoopTask &task) -> std::expected<OnLoopResult, OnLoopError>;

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	std::shared_ptr<SharedData> sharedData;
    	const Gpu &gpu;

        bool passthruImageInitialized = false;

    	// Buffer, image and image views.
    	vku::MappedBuffer hoveringNodeIndexBuffer;
    	std::unique_ptr<vku::AllocatedImage> jumpFloodImage = nullptr;
    	std::unique_ptr<vk::raii::ImageView> jumpFloodPingImageView = nullptr, jumpFloodPongImageView = nullptr;

    	// Attachment groups.
    	std::unique_ptr<vku::AttachmentGroup>     depthPrepassAttachmentGroup = nullptr;
    	std::unique_ptr<vku::MsaaAttachmentGroup> primaryAttachmentGroup = nullptr;

        // Descriptor/command pools.
    	vk::raii::DescriptorPool descriptorPool      = createDescriptorPool();
        vk::raii::CommandPool    computeCommandPool  = createCommandPool(gpu.queueFamilies.compute),
    						     graphicsCommandPool = createCommandPool(gpu.queueFamilies.graphicsPresent);

    	// Descriptor sets.
    	pipelines::DepthRenderer::DescriptorSets depthSets { *gpu.device, *descriptorPool, sharedData->depthRenderer.descriptorSetLayouts };
    	pipelines::JumpFloodComputer::DescriptorSets jumpFloodSets { *gpu.device, *descriptorPool, sharedData->jumpFloodComputer.descriptorSetLayouts };
    	pipelines::PrimitiveRenderer::DescriptorSets primitiveSets { *gpu.device, *descriptorPool, sharedData->primitiveRenderer.descriptorSetLayouts };
    	pipelines::SkyboxRenderer::DescriptorSets skyboxSets { *gpu.device, *descriptorPool, sharedData->skyboxRenderer.descriptorSetLayouts };
    	pipelines::OutlineRenderer::DescriptorSets outlineSets { *gpu.device, *descriptorPool, sharedData->outlineRenderer.descriptorSetLayouts };
    	pipelines::Rec709Renderer::DescriptorSets rec709Sets { *gpu.device, *descriptorPool, sharedData->rec709Renderer.descriptorSetLayouts };

    	// Command buffers.
    	vk::CommandBuffer depthPrepassCommandBuffer, drawCommandBuffer, compositeCommandBuffer;
    	vk::CommandBuffer jumpFloodCommandBuffer;

		// Synchronization stuffs.
		vk::raii::Semaphore depthPrepassFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
    					    swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} },
    						drawFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
    						compositeFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
    						jumpFloodFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
		vk::raii::Fence     inFlightFence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } };

    	[[nodiscard]] auto createJumpFloodImage(const vk::Extent2D &extent) const -> decltype(jumpFloodImage);
    	[[nodiscard]] auto createJumpFloodImageView(std::uint32_t arrayLayer) const -> std::unique_ptr<vk::raii::ImageView>;
    	[[nodiscard]] auto createDepthPrepassAttachmentGroup(const vk::Extent2D &extent) const -> decltype(depthPrepassAttachmentGroup);
    	[[nodiscard]] auto createPrimaryAttachmentGroup(const vk::Extent2D &extent) const -> decltype(primaryAttachmentGroup);
    	[[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);
    	[[nodiscard]] auto createCommandPool(std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto initAttachmentLayouts() const -> void;

    	auto update(const OnLoopTask &task, OnLoopResult &result) -> void;

    	auto recordDepthPrepassCommands(vk::CommandBuffer cb, const OnLoopTask &task) const -> void;
    	// Return false if last jump flood calculation direction is forward (result is in pong buffer), true if
    	// backward, nullopt if JFA not calculated (both hoveringNodeIndex and selectedNodeIndex is nullopt).
		[[nodiscard]] auto recordJumpFloodCalculationCommands(vk::CommandBuffer cb, const OnLoopTask &task) const -> std::optional<bool>;
    	auto recordGltfPrimitiveDrawCommands(vk::CommandBuffer cb, const OnLoopTask &task) const -> void;
    	auto recordPostCompositionCommands(vk::CommandBuffer cb, std::optional<bool> isJumpFloodResultForward, std::uint32_t swapchainImageIndex, const OnLoopTask &task) const -> void;
    };
}
