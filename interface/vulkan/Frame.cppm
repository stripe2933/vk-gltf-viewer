module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.Frame;

import std;
export import vku;
export import :AppState;
export import :gltf.AssetResources;
export import :gltf.SceneResources;
export import :vulkan.SharedData;
import :vulkan.attachment_groups;

namespace vk_gltf_viewer::vulkan {
    export class Frame {
    public:
    	struct ExecutionTask {
    		vk::Rect2D passthruRect;
    		struct { glm::mat4 view, projection; } camera;
    		std::optional<vk::Offset2D> mouseCursorOffset;
    		std::optional<std::uint32_t> hoveringNodeIndex, selectedNodeIndex;
    		std::optional<AppState::Outline> hoveringNodeOutline, selectedNodeOutline;
    		bool useBlurredSkybox;
    		std::optional<std::pair<vk::SurfaceKHR, vk::Extent2D>> swapchainResizeHandleInfo;
    	};

		struct ExecutionResult {
			std::optional<std::uint32_t> hoveringNodeIndex;
			bool presentSuccess;
		};

    	enum class ExecutionError {
    		SwapchainAcquireFailed,
    	};

    	Frame(const Gpu &gpu [[clang::lifetimebound]], const SharedData &sharedData [[clang::lifetimebound]], const gltf::AssetResources &assetResources [[clang::lifetimebound]], const gltf::SceneResources &sceneResources [[clang::lifetimebound]]);

    	[[nodiscard]] auto execute(const ExecutionTask &task) -> std::expected<ExecutionResult, ExecutionError>;

    private:
    	struct JumpFloodResources {
    		vku::AllocatedImage image;
    		vk::raii::ImageView pingImageView, pongImageView;

    		JumpFloodResources(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &extent);
    	};

    	class PassthruResources {
    	public:
    		JumpFloodResources hoveringNodeOutlineJumpFloodResources;
    		JumpFloodResources selectedNodeOutlineJumpFloodResources;

    		// Attachment groups.
    		DepthPrepassAttachmentGroup depthPrepassAttachmentGroup;
    		PrimaryAttachmentGroup primaryAttachmentGroup;

    		PassthruResources(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &extent, vk::CommandBuffer graphicsCommandBuffer);

    	private:
    		auto recordInitialImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    	};

    	const Gpu &gpu;
    	const SharedData &sharedData;
    	const gltf::AssetResources &assetResources;
    	const gltf::SceneResources &sceneResources;

    	// Buffer, image and image views.
    	vku::MappedBuffer hoveringNodeIndexBuffer;
    	std::optional<vk::Extent2D> passthruExtent = std::nullopt;
		std::optional<PassthruResources> passthruResources = std::nullopt;

        // Descriptor/command pools.
    	vk::raii::DescriptorPool descriptorPool = createDescriptorPool();
        vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
    	vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };

    	// Descriptor sets.
    	pipeline::DepthRenderer::DescriptorSets depthSets { *gpu.device, *descriptorPool, sharedData.depthRenderer.descriptorSetLayouts };
    	pipeline::AlphaMaskedDepthRenderer::DescriptorSets alphaMaskedDepthSets { *gpu.device, *descriptorPool, sharedData.alphaMaskedDepthRenderer.descriptorSetLayouts };
    	pipeline::JumpFloodComputer::DescriptorSets hoveringNodeJumpFloodSets { *gpu.device, *descriptorPool, sharedData.jumpFloodComputer.descriptorSetLayouts };
		pipeline::JumpFloodComputer::DescriptorSets selectedNodeJumpFloodSets { *gpu.device, *descriptorPool, sharedData.jumpFloodComputer.descriptorSetLayouts };
    	pipeline::OutlineRenderer::DescriptorSets hoveringNodeOutlineSets { *gpu.device, *descriptorPool, sharedData.outlineRenderer.descriptorSetLayouts };
    	pipeline::OutlineRenderer::DescriptorSets selectedNodeOutlineSets { *gpu.device, *descriptorPool, sharedData.outlineRenderer.descriptorSetLayouts };
    	// Note that we'll use the same descriptor sets for AlphaMaskedPrimitiveRenderer since it has same descriptor set layouts as PrimitiveRenderer.
    	pipeline::PrimitiveRenderer::DescriptorSets primitiveSets { *gpu.device, *descriptorPool, sharedData.primitiveRenderer.descriptorSetLayouts };
    	pipeline::Rec709Renderer::DescriptorSets rec709Sets { *gpu.device, *descriptorPool, sharedData.rec709Renderer.descriptorSetLayouts };
    	pipeline::SkyboxRenderer::DescriptorSets skyboxSets { *gpu.device, *descriptorPool, sharedData.skyboxRenderer.descriptorSetLayouts };
    	pipeline::SphericalHarmonicsRenderer::DescriptorSets sphericalHarmonicsSets { *gpu.device, *descriptorPool, sharedData.sphericalHarmonicsRenderer.descriptorSetLayouts };

    	// Command buffers.
    	vk::CommandBuffer depthPrepassCommandBuffer;
    	vk::CommandBuffer drawCommandBuffer;
    	vk::CommandBuffer compositeCommandBuffer;
    	vk::CommandBuffer jumpFloodCommandBuffer;

		// Synchronization stuffs.
		vk::raii::Semaphore depthPrepassFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
    	vk::raii::Semaphore swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} };
    	vk::raii::Semaphore drawFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
    	vk::raii::Semaphore compositeFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
    	vk::raii::Semaphore jumpFloodFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
		vk::raii::Fence inFlightFence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } };

    	[[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;
    	auto update(const ExecutionTask &task, ExecutionResult &result) -> void;

    	auto recordDepthPrepassCommands(vk::CommandBuffer cb, const ExecutionTask &task) const -> void;
    	// Return true if last jump flood calculation direction is forward (result is in pong image), false if backward.
		[[nodiscard]] auto recordJumpFloodComputeCommands(
			vk::CommandBuffer cb,
			const vku::Image &image,
			const pipeline::JumpFloodComputer::DescriptorSets &descriptorSets,
			std::uint32_t initialSampleOffset) const -> bool;
    	auto recordGltfPrimitiveDrawCommands(vk::CommandBuffer cb, const ExecutionTask &task) const -> void;
    	auto recordPostCompositionCommands(
    		vk::CommandBuffer cb,
    		std::optional<bool> hoveringNodeJumpFloodForward,
    		std::optional<bool> selectedNodeJumpFloodForward,
    		std::uint32_t swapchainImageIndex,
    		const ExecutionTask &task) const -> void;
    };
}