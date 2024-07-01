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
export import :AppState;
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
    		std::optional<AppState::Outline> hoveringNodeOutline, selectedNodeOutline;
    		bool useBlurredSkybox;
    		std::optional<std::pair<vk::SurfaceKHR, vk::Extent2D>> swapchainResizeHandleInfo;
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

    private:
    	struct JumpFloodResources {
    		vku::AllocatedImage image;
    		vk::raii::ImageView pingImageView, pongImageView;

    		JumpFloodResources(const Gpu &gpu, const vk::Extent2D &extent);
    	};

    	class PassthruExtentDependentResources {
    	public:
			vk::Extent2D extent; // Extent that used as the resources initialization.

    		JumpFloodResources hoveringNodeOutlineJumpFloodResources,
    						   selectedNodeOutlineJumpFloodResources;

    		// Attachment groups.
    		vku::AttachmentGroup     depthPrepassAttachmentGroup;
    		vku::MsaaAttachmentGroup primaryAttachmentGroup;

    		PassthruExtentDependentResources(const Gpu &gpu, const vk::Extent2D &extent, vk::CommandBuffer graphicsCommandBuffer);

    	private:
    		[[nodiscard]] auto createDepthPrepassAttachmentGroup(const Gpu &gpu) const -> decltype(depthPrepassAttachmentGroup);
    		[[nodiscard]] auto createPrimaryAttachmentGroup(const Gpu &gpu) const -> decltype(primaryAttachmentGroup);

    		auto recordInitialImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    	};

    	std::shared_ptr<SharedData> sharedData;
    	const Gpu &gpu;

    	// Buffer, image and image views.
    	vku::MappedBuffer hoveringNodeIndexBuffer;
		std::optional<PassthruExtentDependentResources> passthruExtentDependentResources = std::nullopt;

        // Descriptor/command pools.
    	vk::raii::DescriptorPool descriptorPool      = createDescriptorPool();
        vk::raii::CommandPool    computeCommandPool  = createCommandPool(gpu.queueFamilies.compute),
    						     graphicsCommandPool = createCommandPool(gpu.queueFamilies.graphicsPresent);

    	// Descriptor sets.
    	pipelines::DepthRenderer::DescriptorSets              depthSets { *gpu.device, *descriptorPool, sharedData->depthRenderer.descriptorSetLayouts };
    	pipelines::AlphaMaskedDepthRenderer::DescriptorSets   alphaMaskedDepthSets { *gpu.device, *descriptorPool, sharedData->alphaMaskedDepthRenderer.descriptorSetLayouts };
    	pipelines::JumpFloodComputer::DescriptorSets          hoveringNodeJumpFloodSets { *gpu.device, *descriptorPool, sharedData->jumpFloodComputer.descriptorSetLayouts },
												              selectedNodeJumpFloodSets { *gpu.device, *descriptorPool, sharedData->jumpFloodComputer.descriptorSetLayouts };
    	pipelines::OutlineRenderer::DescriptorSets            hoveringNodeOutlineSets { *gpu.device, *descriptorPool, sharedData->outlineRenderer.descriptorSetLayouts },
    											              selectedNodeOutlineSets { *gpu.device, *descriptorPool, sharedData->outlineRenderer.descriptorSetLayouts };
    	// Note that we'll use the same descriptor sets for AlphaMaskedPrimitiveRenderer since it has same descriptor set layouts as PrimitiveRenderer.
    	pipelines::PrimitiveRenderer::DescriptorSets          primitiveSets { *gpu.device, *descriptorPool, sharedData->primitiveRenderer.descriptorSetLayouts };
    	pipelines::Rec709Renderer::DescriptorSets             rec709Sets { *gpu.device, *descriptorPool, sharedData->rec709Renderer.descriptorSetLayouts };
    	pipelines::SkyboxRenderer::DescriptorSets             skyboxSets { *gpu.device, *descriptorPool, sharedData->skyboxRenderer.descriptorSetLayouts };
    	pipelines::SphericalHarmonicsRenderer::DescriptorSets sphericalHarmonicsSets { *gpu.device, *descriptorPool, sharedData->sphericalHarmonicsRenderer.descriptorSetLayouts };

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

    	[[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);
    	[[nodiscard]] auto createCommandPool(std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;
    	auto update(const OnLoopTask &task, OnLoopResult &result) -> void;

    	auto recordDepthPrepassCommands(vk::CommandBuffer cb, const OnLoopTask &task) const -> void;
    	// Return true if last jump flood calculation direction is forward (result is in pong image), false if backward.
		[[nodiscard]] auto recordJumpFloodComputeCommands(
			vk::CommandBuffer cb,
			const vku::Image &image,
			const pipelines::JumpFloodComputer::DescriptorSets &descriptorSets,
			std::uint32_t initialSampleOffset) const -> bool;
    	auto recordGltfPrimitiveDrawCommands(vk::CommandBuffer cb, const OnLoopTask &task) const -> void;
    	auto recordPostCompositionCommands(
    		vk::CommandBuffer cb,
    		std::optional<bool> hoveringNodeJumpFloodForward,
    		std::optional<bool> selectedNodeJumpFloodForward,
    		std::uint32_t swapchainImageIndex,
    		const OnLoopTask &task) const -> void;
    };
}
