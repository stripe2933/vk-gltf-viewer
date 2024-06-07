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

        // Descriptor/command pools.
        vk::raii::CommandPool graphicsCommandPool;

    	// Command buffers.
    	vk::CommandBuffer drawCommandBuffer;

		// Synchronization stuffs.
		vk::raii::Semaphore swapchainImageAcquireSema, drawFinishSema;
		vk::raii::Fence inFlightFence;

    	Frame(const Gpu &gpu, const std::shared_ptr<SharedData> &sharedData);

    	// Return true if frame's corresponding swapchain image sucessfully presented, false otherwise (e.g. swapchain out of date).
    	[[nodiscard]] auto onLoop(const Gpu &gpu) const -> bool;

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createCommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto draw(vk::CommandBuffer cb, const vku::AttachmentGroup &attachmentGroup) const -> void;
    };
}
