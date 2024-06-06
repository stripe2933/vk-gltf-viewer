module;

#include <compare>
#include <vector>

#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.frame.SharedData;

export import vku;
export import :vulkan.Gpu;
export import :vulkan.pipelines;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
    public:
    	// Swapchain.
		vk::raii::SwapchainKHR swapchain;
		vk::Extent2D swapchainExtent;
		std::vector<vk::Image> swapchainImages = swapchain.getImages();

		// Pipelines.
		TriangleRenderer triangleRenderer;

    	// Attachment groups.
    	std::vector<vku::AttachmentGroup> swapchainAttachmentGroups;

    	// Descriptor/command pools.
    	vk::raii::CommandPool graphicsCommandPool;

    	SharedData(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent, const shaderc::Compiler &compiler = {});

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createSwapchain(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain);
    	[[nodiscard]] auto createSwapchainAttachmentGroups(const vk::raii::Device &device) const -> decltype(swapchainAttachmentGroups);
    	[[nodiscard]] auto createCommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto initAttachmentLayouts(const Gpu &gpu) const -> void;
    };
}
