module;

#include <cstdint>
#include <array>
#include <compare>
#include <memory>

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
    	vku::MappedBuffer hoveringNodeIdBuffer;

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

    	// Command buffers.
    	vk::CommandBuffer depthPrepassCommandBuffer, drawCommandBuffer, blitToSwapchainCommandBuffer;
    	vk::CommandBuffer jumpFloodCommandBuffer;

		// Synchronization stuffs.
		vk::raii::Semaphore depthPrepassFinishSema, swapchainImageAcquireSema, drawFinishSema, blitToSwapchainFinishSema;
		vk::raii::Semaphore jumpFloodFinishSema;
		vk::raii::Fence inFlightFence;

    	Frame(GlobalState &globalState, const std::shared_ptr<SharedData> &sharedData,  const Gpu &gpu);

    	// Return true if frame's corresponding swapchain image sucessfully presented, false otherwise (e.g. swapchain out of date).
    	[[nodiscard]] auto onLoop(const Gpu &gpu) -> bool;

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	std::uint32_t hoveringNodeIndex = std::numeric_limits<std::uint32_t>::max();
		vk::Bool32 isJumpFloodResultForward;

    	[[nodiscard]] auto createJumpFloodImage(vma::Allocator allocator) const -> decltype(jumpFloodImage);
    	[[nodiscard]] auto createJumpFloodImageViews(const vk::raii::Device &device) const -> decltype(jumpFloodImageViews);
    	[[nodiscard]] auto createDepthPrepassAttachmentGroup(const Gpu &gpu) const -> decltype(depthPrepassAttachmentGroup);
    	[[nodiscard]] auto createPrimaryAttachmentGroup(const Gpu &gpu) const -> decltype(primaryAttachmentGroup);
    	[[nodiscard]] auto createDescriptorPool(const vk::raii::Device &device) const -> decltype(descriptorPool);
    	[[nodiscard]] auto createCommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto initAttachmentLayouts(const Gpu &gpu) const -> void;

    	auto update() -> void;
    	auto depthPrepass(const Gpu &gpu, vk::CommandBuffer cb) const -> void;
    	auto jumpFlood(const Gpu &gpu, vk::CommandBuffer cb) -> void;
    	auto draw(vk::CommandBuffer cb) const -> void;
    	auto blitToSwapchain(const Gpu &gpu, vk::CommandBuffer cb, const vku::AttachmentGroup &swapchainAttachmentGroup) const -> void;
    };
}
