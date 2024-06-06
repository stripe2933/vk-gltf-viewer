module;

#include <cstdint>
#include <format>
#include <limits>
#include <mutex>
#include <stdexcept>

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.frame.Frame;

vk_gltf_viewer::vulkan::Frame::Frame(
	const Gpu &gpu,
	const std::shared_ptr<SharedData> &sharedData
) : sharedData { sharedData },
	graphicsCommandPool { createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent) },
	swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} },
	drawFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	inFlightFence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } } {
	drawCommandBuffer = (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
		*graphicsCommandPool,
		vk::CommandBufferLevel::ePrimary,
		1,
	}).front();
}

auto vk_gltf_viewer::vulkan::Frame::onLoop(
	const Gpu &gpu
) const -> bool {
	constexpr std::uint64_t MAX_TIMEOUT = std::numeric_limits<std::uint64_t>::max();

	// Wait for the previous frame execution to finish.
	if (auto result = gpu.device.waitForFences(*inFlightFence, true, MAX_TIMEOUT); result != vk::Result::eSuccess) {
		throw std::runtime_error{ std::format("Failed to wait for in-flight fence: {}", to_string(result)) };
	}
	gpu.device.resetFences(*inFlightFence);

	// Acquire the next swapchain image.
	std::uint32_t imageIndex;
	try {
		imageIndex = (*gpu.device).acquireNextImageKHR(*sharedData->swapchain, MAX_TIMEOUT, *swapchainImageAcquireSema).value;
	}
	catch (const vk::OutOfDateKHRError&) {
		return false;
	}

	// Record draw command.
	drawCommandBuffer.begin(vk::CommandBufferBeginInfo{});

	// Change image layout to ColorAttachmentOptimal.
	drawCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{}, {}, {},
		vk::ImageMemoryBarrier{
			{}, vk::AccessFlagBits::eColorAttachmentWrite,
			vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			sharedData->swapchainImages[imageIndex],
			vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
		});

	// Begin dynamic rendering.
	drawCommandBuffer.beginRenderingKHR(
		sharedData->swapchainAttachmentGroups[imageIndex].getRenderingInfo(
			std::array {
				std::tuple { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::ClearColorValue{} },
			}));

	// Set viewport and scissor.
	sharedData->swapchainAttachmentGroups[imageIndex].setViewport(drawCommandBuffer);
	sharedData->swapchainAttachmentGroups[imageIndex].setScissor(drawCommandBuffer);

	// Draw triangle.
	sharedData->triangleRenderer.draw(drawCommandBuffer);

	// End dynamic rendering.
	drawCommandBuffer.endRenderingKHR();

	// Change image layout to PresentSrcKHR.
	drawCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		vk::ImageMemoryBarrier{
			vk::AccessFlagBits::eColorAttachmentWrite, {},
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			sharedData->swapchainImages[imageIndex],
			vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
		});

	drawCommandBuffer.end();

	// Submit draw command to the graphics queue.
	constexpr vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	gpu.queues.graphicsPresent.submit(vk::SubmitInfo{
		*swapchainImageAcquireSema,
		waitStages,
		drawCommandBuffer,
		*drawFinishSema,
	}, *inFlightFence);

	// Present the image to the swapchain.
	try {
		// The result codes VK_ERROR_OUT_OF_DATE_KHR and VK_SUBOPTIMAL_KHR have the same meaning when
		// returned by vkQueuePresentKHR as they do when returned by vkAcquireNextImageKHR.
		if (gpu.queues.graphicsPresent.presentKHR({ *drawFinishSema, *sharedData->swapchain, imageIndex }) == vk::Result::eSuboptimalKHR) {
			throw vk::OutOfDateKHRError { "Suboptimal swapchain" };
		}
	}
	catch (const vk::OutOfDateKHRError&) {
		return false;
	}

	return true;
}

auto vk_gltf_viewer::vulkan::Frame::handleSwapchainResize(
	const Gpu &gpu,
	vk::SurfaceKHR surface,
	const vk::Extent2D &newExtent
) -> void {

}

auto vk_gltf_viewer::vulkan::Frame::createCommandPool(
    const vk::raii::Device &device,
    std::uint32_t queueFamilyIndex
) const -> vk::raii::CommandPool {
	return { device, vk::CommandPoolCreateInfo{
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyIndex,
	} };
}