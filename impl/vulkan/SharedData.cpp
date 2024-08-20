module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.SharedData;

import std;

vk_gltf_viewer::vulkan::SharedData::SharedData(
    const Gpu &gpu,
    vk::SurfaceKHR surface,
	const vk::Extent2D &swapchainExtent,
	const DescriptorSetLayouts &descriptorSetLayouts
) : swapchainExtent { swapchainExtent },
	swapchain { createSwapchain(surface, swapchainExtent) },
	gpu { gpu },
	sceneRenderingPipelineLayout { gpu.device, std::tie(descriptorSetLayouts.imageBasedLighting, descriptorSetLayouts.asset, descriptorSetLayouts.scene) },
	alphaMaskedDepthRenderer { gpu.device, std::tie(descriptorSetLayouts.scene, descriptorSetLayouts.asset) },
	alphaMaskedFacetedPrimitiveRenderer { gpu.device, sceneRenderingPipelineLayout },
	alphaMaskedJumpFloodSeedRenderer { gpu.device, std::tie(descriptorSetLayouts.scene, descriptorSetLayouts.asset) },
	alphaMaskedPrimitiveRenderer { gpu.device, sceneRenderingPipelineLayout },
	depthRenderer { gpu.device, descriptorSetLayouts.scene },
	facetedPrimitiveRenderer { gpu.device, sceneRenderingPipelineLayout },
	jumpFloodComputer { gpu.device },
	jumpFloodSeedRenderer { gpu.device, descriptorSetLayouts.scene },
	outlineRenderer { gpu.device },
	primitiveRenderer { gpu.device, sceneRenderingPipelineLayout },
	skyboxRenderer { gpu.device, descriptorSetLayouts.skybox, cubeIndices } {
	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		recordInitialImageLayoutTransitionCommands(cb);
	});
	gpu.queues.graphicsPresent.waitIdle();
}

auto vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(
	vk::SurfaceKHR surface,
	const vk::Extent2D &newExtent
) -> void {
	swapchain = createSwapchain(surface, newExtent, *swapchain);
	swapchainExtent = newExtent;
	swapchainImages = swapchain.getImages();

	swapchainAttachmentGroups = createSwapchainAttachmentGroups();
	imGuiSwapchainAttachmentGroups = createImGuiSwapchainAttachmentGroups();

	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [this](vk::CommandBuffer cb) {
		recordInitialImageLayoutTransitionCommands(cb);
	});
	gpu.queues.graphicsPresent.waitIdle();
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchain(
	vk::SurfaceKHR surface,
	const vk::Extent2D &extent,
	vk::SwapchainKHR oldSwapchain
) const -> decltype(swapchain) {
	const vk::SurfaceCapabilitiesKHR surfaceCapabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);
	return { gpu.device, vk::StructureChain {
		vk::SwapchainCreateInfoKHR{
			vk::SwapchainCreateFlagBitsKHR::eMutableFormat,
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
		},
		vk::ImageFormatListCreateInfo {
			vku::unsafeProxy({
				vk::Format::eB8G8R8A8Srgb,
				vk::Format::eB8G8R8A8Unorm,
			}),
		},
	}.get() };
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchainAttachmentGroups() const -> std::vector<ag::Swapchain> {
	return { std::from_range, swapchainImages | std::views::transform([&](vk::Image image) {
		return ag::Swapchain { gpu.device, { image, vk::Extent3D { swapchainExtent }, vk::Format::eB8G8R8A8Srgb, 1, 1 } };
	}) };
}

auto vk_gltf_viewer::vulkan::SharedData::createImGuiSwapchainAttachmentGroups() const -> std::vector<ag::ImGuiSwapchain> {
	return { std::from_range, swapchainImages | std::views::transform([&](vk::Image image) {
		return ag::ImGuiSwapchain { gpu.device, { image, vk::Extent3D { swapchainExtent }, vk::Format::eB8G8R8A8Srgb, 1, 1 } };
	}) };
}

auto vk_gltf_viewer::vulkan::SharedData::createCommandPool(
	std::uint32_t queueFamilyIndex
) const -> vk::raii::CommandPool {
	return { gpu.device, vk::CommandPoolCreateInfo{
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyIndex,
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::recordInitialImageLayoutTransitionCommands(
	vk::CommandBuffer graphicsCommandBuffer
) const -> void {
	graphicsCommandBuffer.pipelineBarrier(
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
			| std::ranges::to<std::vector>());
}