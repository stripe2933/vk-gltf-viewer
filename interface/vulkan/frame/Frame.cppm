module;

#include <cstdint>
#include <array>
#include <compare>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

export module vk_gltf_viewer:vulkan.frame.Frame;

export import vku;
export import :GlobalState;
export import :vulkan.Gpu;
export import :vulkan.frame.SharedData;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { (Is, __VA_ARGS__)... }; })

namespace vk_gltf_viewer::vulkan::inline frame {
    export class Frame {
    public:
    	enum class OnLoopError {
    		SwapchainAcquireFailed,
    	};

		struct OnLoopResult {
			std::optional<std::uint32_t> hoveringNodeIndex;
			bool presentSuccess;
		};

    	const GlobalState &globalState;
    	std::shared_ptr<SharedData> sharedData;
    	const Gpu &gpu;

    	// Buffer, image and image views.
    	vku::AllocatedImage jumpFloodImage = createJumpFloodImage();
    	struct { vk::raii::ImageView ping, pong; } jumpFloodImageViews = createJumpFloodImageViews();
    	vku::MappedBuffer hoveringNodeIndexBuffer;

    	// Attachment groups.
    	vku::AttachmentGroup depthPrepassAttachmentGroup = createDepthPrepassAttachmentGroup();
    	vku::MsaaAttachmentGroup primaryAttachmentGroup = createPrimaryAttachmentGroup();

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

    	// Framebuffers.
    	vk::raii::Framebuffer compositionFramebuffer = createCompositionFramebuffer();

		// Synchronization stuffs.
		vk::raii::Semaphore depthPrepassFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
    					    swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} },
    						drawFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
    						compositeFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
    						jumpFloodFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
		vk::raii::Fence     inFlightFence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } };

    	Frame(const GlobalState &globalState, const std::shared_ptr<SharedData> &sharedData, const Gpu &gpu);

    	[[nodiscard]] auto onLoop() -> std::expected<OnLoopResult, OnLoopError>;

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createJumpFloodImage() const -> decltype(jumpFloodImage);
    	[[nodiscard]] auto createJumpFloodImageViews() const -> decltype(jumpFloodImageViews);
    	[[nodiscard]] auto createDepthPrepassAttachmentGroup() const -> decltype(depthPrepassAttachmentGroup);
    	[[nodiscard]] auto createPrimaryAttachmentGroup() const -> decltype(primaryAttachmentGroup);
    	[[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);
    	[[nodiscard]] auto createCommandPool(std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;
    	[[nodiscard]] auto createCompositionFramebuffer() const -> decltype(compositionFramebuffer);

    	auto initAttachmentLayouts() const -> void;

    	auto update(OnLoopResult &result) -> void;

    	auto recordDepthPrepassCommands(vk::CommandBuffer cb) const -> void;
    	// Return false if last jump flood calculation direction is forward (result is in pong buffer), true if
    	// backward, nullopt if JFA not calculated (both hoveringNodeIndex and selectedNodeIndex is nullopt).
		[[nodiscard]] auto recordJumpFloodCalculationCommands(vk::CommandBuffer cb) const -> std::optional<bool>;
    	auto recordGltfPrimitiveDrawCommands(vk::CommandBuffer cb) const -> void;
    	auto recordPostCompositionCommands(vk::CommandBuffer cb, std::optional<bool> isJumpFloodResultForward, std::uint32_t swapchainImageIndex) const -> void;
    };
}
