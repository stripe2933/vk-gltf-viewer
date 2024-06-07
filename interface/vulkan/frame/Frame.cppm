module;

#include <compare>
#include <memory>

export module vk_gltf_viewer:vulkan.frame.Frame;

export import vku;
export import :vulkan.Gpu;
export import :vulkan.frame.SharedData;

namespace vk_gltf_viewer::vulkan {
    export class Frame {
    public:
    	std::shared_ptr<SharedData> sharedData;

    	// Attachment groups.
    	vku::MsaaAttachmentGroup primaryAttachmentGroup;

        // Descriptor/command pools.
        vk::raii::CommandPool graphicsCommandPool;

    	// Command buffers.
    	vk::CommandBuffer drawCommandBuffer, blitToSwapchainCommandBuffer;

		// Synchronization stuffs.
		vk::raii::Semaphore swapchainImageAcquireSema, drawFinishSema, blitToSwapchainFinishSema;
		vk::raii::Fence inFlightFence;

    	Frame(const Gpu &gpu, const std::shared_ptr<SharedData> &sharedData);

    	// Return true if frame's corresponding swapchain image sucessfully presented, false otherwise (e.g. swapchain out of date).
    	[[nodiscard]] auto onLoop(const Gpu &gpu) const -> bool;

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createPrimaryAttachmentGroup(const Gpu &gpu) const -> decltype(primaryAttachmentGroup);
    	[[nodiscard]] auto createCommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto initAttachmentLayouts(const Gpu &gpu) const -> void;

    	auto draw(vk::CommandBuffer cb) const -> void;
    	auto blitToSwapchain(vk::CommandBuffer cb, const vku::AttachmentGroup &swapchainAttachmentGroup) const -> void;
    };
}
