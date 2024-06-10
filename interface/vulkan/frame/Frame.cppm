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
    	vk::raii::DescriptorPool descriptorPool;
        vk::raii::CommandPool graphicsCommandPool;

    	// Buffer, image and image views.
    	vku::MappedBuffer cameraBuffer;

    	// Descriptor sets.
    	pipelines::MeshRenderer::DescriptorSets meshRendererSets;
    	pipelines::SkyboxRenderer::DescriptorSets skyboxSets;

    	// Command buffers.
    	vk::CommandBuffer drawCommandBuffer, blitToSwapchainCommandBuffer;

		// Synchronization stuffs.
		vk::raii::Semaphore swapchainImageAcquireSema, drawFinishSema, blitToSwapchainFinishSema;
		vk::raii::Fence inFlightFence;

    	Frame(const Gpu &gpu, const std::shared_ptr<SharedData> &sharedData);

    	// Return true if frame's corresponding swapchain image sucessfully presented, false otherwise (e.g. swapchain out of date).
    	[[nodiscard]] auto onLoop(const Gpu &gpu) -> bool;

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createPrimaryAttachmentGroup(const Gpu &gpu) const -> decltype(primaryAttachmentGroup);
    	[[nodiscard]] auto createDescriptorPool(const vk::raii::Device &device) const -> decltype(descriptorPool);
    	[[nodiscard]] auto createCameraBuffer(vma::Allocator allocator) const -> decltype(cameraBuffer);
    	[[nodiscard]] auto createCommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto initAttachmentLayouts(const Gpu &gpu) const -> void;

    	auto update() -> void;
    	auto draw(vk::CommandBuffer cb) const -> void;
    	auto blitToSwapchain(vk::CommandBuffer cb, const vku::AttachmentGroup &swapchainAttachmentGroup) const -> void;
    };
}
