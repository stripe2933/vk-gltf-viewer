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

namespace vk_gltf_viewer::vulkan::inline frame {
    export class Frame {
    public:
    	GlobalState &globalState;
    	std::shared_ptr<SharedData> sharedData;

    	// Buffer, image and image views.
    	vku::AllocatedImage jumpFloodImage;
    	std::array<vk::raii::ImageView, 2> jumpFloodImageViews;
    	vku::MappedBuffer hoveringNodeIndexBuffer;

    	// Attachment groups.
    	vku::AttachmentGroup depthPrepassAttachmentGroup;
    	vku::MsaaAttachmentGroup primaryAttachmentGroup;

        // Descriptor/command pools.
    	vk::raii::DescriptorPool descriptorPool;
        vk::raii::CommandPool computeCommandPool, graphicsCommandPool;

    	// Descriptor sets.
    	pipelines::DepthRenderer::DescriptorSets depthSets;
    	pipelines::JumpFloodComputer::DescriptorSets jumpFloodSets;
    	pipelines::PrimitiveRenderer::DescriptorSets primitiveSets;
    	pipelines::SkyboxRenderer::DescriptorSets skyboxSets;
    	std::array<pipelines::OutlineRenderer::DescriptorSets, 2> outlineSets;
    	pipelines::Rec709Renderer::DescriptorSets rec709Sets;

    	// Command buffers.
    	vk::CommandBuffer depthPrepassCommandBuffer, drawCommandBuffer, compositeCommandBuffer;
    	vk::CommandBuffer jumpFloodCommandBuffer;

    	// Framebuffers.
    	vk::raii::Framebuffer compositionFramebuffer;

		// Synchronization stuffs.
		vk::raii::Semaphore depthPrepassFinishSema, swapchainImageAcquireSema, drawFinishSema, compositeFinishSema;
		vk::raii::Semaphore jumpFloodFinishSema;
		vk::raii::Fence inFlightFence;

    	Frame(GlobalState &globalState, const std::shared_ptr<SharedData> &sharedData,  const Gpu &gpu);

    	// Return true if frame's corresponding swapchain image sucessfully presented, false otherwise (e.g. swapchain out of date).
    	[[nodiscard]] auto onLoop(const Gpu &gpu) -> bool;

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	std::optional<std::uint32_t> hoveringNodeIndex = std::nullopt;

    	[[nodiscard]] auto createJumpFloodImage(vma::Allocator allocator) const -> decltype(jumpFloodImage);
    	[[nodiscard]] auto createJumpFloodImageViews(const vk::raii::Device &device) const -> decltype(jumpFloodImageViews);
    	[[nodiscard]] auto createDepthPrepassAttachmentGroup(const Gpu &gpu) const -> decltype(depthPrepassAttachmentGroup);
    	[[nodiscard]] auto createPrimaryAttachmentGroup(const Gpu &gpu) const -> decltype(primaryAttachmentGroup);
    	[[nodiscard]] auto createDescriptorPool(const vk::raii::Device &device) const -> decltype(descriptorPool);
    	[[nodiscard]] auto createCompositionFramebuffer(const vk::raii::Device &device) const -> decltype(compositionFramebuffer);

    	auto initAttachmentLayouts(const Gpu &gpu) const -> void;

    	auto update() -> void;
    	auto depthPrepass(const Gpu &gpu, vk::CommandBuffer cb) const -> void;
    	// Return false if jumpFloodResult is in arrayLayer=0, true if in arrayLayer=1, nullopt if JFA not calculated (hoveringNodeIndex is nullopt).
		[[nodiscard]] auto jumpFlood(const Gpu &gpu, vk::CommandBuffer cb) const -> std::optional<bool>;
    	auto draw(vk::CommandBuffer cb) const -> void;
    	auto composite(const Gpu &gpu, vk::CommandBuffer cb, const std::optional<bool> &isJumpFloodResultForward, std::uint32_t swapchainImageIndex) const -> void;
    };
}
