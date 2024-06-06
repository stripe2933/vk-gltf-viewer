module;

#include <ranges>
#include <vector>

#include <shaderc/shaderc.hpp>

module vk_gltf_viewer;
import :vulkan.frame.SharedData;

vk_gltf_viewer::vulkan::SharedData::SharedData(
    const Gpu &gpu,
    vk::SurfaceKHR surface,
	const vk::Extent2D &swapchainExtent,
    const shaderc::Compiler &compiler
) : swapchain { createSwapchain(gpu, surface, swapchainExtent) },
	swapchainExtent { swapchainExtent },
	triangleRenderer { gpu.device, compiler },
	swapchainAttachmentGroups { createSwapchainAttachmentGroups(gpu.device) },
	graphicsCommandPool { createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent) } {
	initAttachmentLayouts(gpu);
}

auto vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(
	const Gpu &gpu,
	vk::SurfaceKHR surface,
	const vk::Extent2D &newExtent
) -> void {
	swapchain = createSwapchain(gpu, surface, newExtent, *swapchain);
	swapchainExtent = newExtent;
	swapchainImages = swapchain.getImages();

	swapchainAttachmentGroups = createSwapchainAttachmentGroups(gpu.device);

	initAttachmentLayouts(gpu);
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchain(
	const Gpu &gpu,
	vk::SurfaceKHR surface,
	const vk::Extent2D &extent,
	vk::SwapchainKHR oldSwapchain
) const -> decltype(swapchain) {
	const vk::SurfaceCapabilitiesKHR surfaceCapabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);
	return { gpu.device, vk::SwapchainCreateInfoKHR{
		{},
		surface,
		std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount),
		vk::Format::eB8G8R8A8Srgb,
		vk::ColorSpaceKHR::eSrgbNonlinear,
		extent,
		1,
		vk::ImageUsageFlagBits::eColorAttachment,
		{}, {},
		surfaceCapabilities.currentTransform,
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		vk::PresentModeKHR::eFifo,
		true,
		oldSwapchain,
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchainAttachmentGroups(
	const vk::raii::Device &device
) const -> decltype(swapchainAttachmentGroups) {
	return swapchainImages
		| std::views::transform([&](vk::Image image) {
			vku::AttachmentGroup attachmentGroup { swapchainExtent };
			attachmentGroup.addColorAttachment(
				device,
				{ image, vk::Extent3D { swapchainExtent, 1 }, vk::Format::eB8G8R8A8Srgb, 1, 1 });
			return attachmentGroup;
		})
		| std::ranges::to<std::vector<vku::AttachmentGroup>>();
}

auto vk_gltf_viewer::vulkan::SharedData::createCommandPool(
	const vk::raii::Device &device,
	std::uint32_t queueFamilyIndex
) const -> vk::raii::CommandPool {
	return { device, vk::CommandPoolCreateInfo{
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyIndex,
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::initAttachmentLayouts(
	const Gpu &gpu
) const -> void {
	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, {}, {},
			swapchainImages
				| std::views::transform([](vk::Image image) {
					return vk::ImageMemoryBarrier{
						{}, {},
						{}, vk::ImageLayout::ePresentSrcKHR,
						vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
						image,
						vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
					};
				})
				| std::ranges::to<std::vector<vk::ImageMemoryBarrier>>());
	});
	gpu.queues.graphicsPresent.waitIdle();
}