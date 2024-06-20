module;

#include <cstdint>
#include <array>
#include <compare>
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
    	GlobalState &globalState;
    	std::shared_ptr<SharedData> sharedData;
    	const Gpu &gpu;

    	// Buffer, image and image views.
    	vku::AllocatedImage jumpFloodImage = createJumpFloodImage();
    	std::array<vk::raii::ImageView, 2> jumpFloodImageViews = createJumpFloodImageViews();
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
    	std::array<pipelines::OutlineRenderer::DescriptorSets, 2> outlineSets
    		= ARRAY_OF(2, pipelines::OutlineRenderer::DescriptorSets { *gpu.device, *descriptorPool, sharedData->outlineRenderer.descriptorSetLayouts });
    	pipelines::Rec709Renderer::DescriptorSets rec709Sets = { *gpu.device, *descriptorPool, sharedData->rec709Renderer.descriptorSetLayouts };

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

    	Frame(GlobalState &globalState, const std::shared_ptr<SharedData> &sharedData, const Gpu &gpu);

    	// Return true if frame's corresponding swapchain image sucessfully presented, false otherwise (e.g. swapchain out of date).
    	[[nodiscard]] auto onLoop() -> bool;

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	std::optional<std::uint32_t> hoveringNodeIndex = std::nullopt;

    	[[nodiscard]] auto createJumpFloodImage() const -> decltype(jumpFloodImage);
    	[[nodiscard]] auto createJumpFloodImageViews() const -> decltype(jumpFloodImageViews);
    	[[nodiscard]] auto createDepthPrepassAttachmentGroup() const -> decltype(depthPrepassAttachmentGroup);
    	[[nodiscard]] auto createPrimaryAttachmentGroup() const -> decltype(primaryAttachmentGroup);
    	[[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);
    	[[nodiscard]] auto createCommandPool(std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;
    	[[nodiscard]] auto createCompositionFramebuffer() const -> decltype(compositionFramebuffer);

    	auto initAttachmentLayouts() const -> void;

    	auto update() -> void;
    	auto depthPrepass(vk::CommandBuffer cb) const -> void;
    	// Return false if jumpFloodResult is in arrayLayer=0, true if in arrayLayer=1, nullopt if JFA not calculated (hoveringNodeIndex is nullopt).
		[[nodiscard]] auto jumpFlood(vk::CommandBuffer cb) const -> std::optional<bool>;
    	auto draw(vk::CommandBuffer cb) const -> void;
    	auto composite(vk::CommandBuffer cb, const std::optional<bool> &isJumpFloodResultForward, std::uint32_t swapchainImageIndex) const -> void;
    };
}
