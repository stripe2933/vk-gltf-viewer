module;

#include <cstdint>
#include <format>
#include <limits>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <unordered_map>

#include <fastgltf/core.hpp>
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

	// Record commands.
	draw(drawCommandBuffer, sharedData->swapchainAttachmentGroups[imageIndex]);

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

auto vk_gltf_viewer::vulkan::Frame::draw(
	vk::CommandBuffer cb,
	const vku::AttachmentGroup &attachmentGroup
) const -> void {
	cb.begin(vk::CommandBufferBeginInfo{});

	// Change image layout to ColorAttachmentOptimal.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{}, {}, {},
		vk::ImageMemoryBarrier{
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eColorAttachmentWrite,
			vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			attachmentGroup.colorAttachments[0].image,
			vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
		});

	// Begin dynamic rendering.
	cb.beginRenderingKHR(attachmentGroup.getRenderingInfo(
		std::array {
			std::tuple { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::ClearColorValue{} },
		}));

	// Set viewport and scissor.
	attachmentGroup.setViewport(cb, true);
	attachmentGroup.setScissor(cb);

	// Draw glTF mesh.
	constexpr glm::vec3 viewPosition { 0.5f };
	const glm::mat4 projectionView
		= glm::gtc::perspective(glm::radians(45.0f), vku::aspect(sharedData->swapchainExtent), 0.1f, 100.0f)
		* glm::gtc::lookAt(viewPosition, glm::vec3{ 0.f }, glm::vec3{ 0.f, 1.f, 0.f });

	const fastgltf::Asset &asset = sharedData->assetResources.asset;
	for (std::stack dfs { std::from_range, asset.scenes[asset.defaultScene.value_or(0)].nodeIndices | std::views::reverse }; !dfs.empty(); ) {
		const std::size_t nodeIndex = dfs.top();
        const fastgltf::Node &node = asset.nodes[nodeIndex];
        if (node.meshIndex) {
        	const glm::mat4 &nodeWorldTransform = sharedData->sceneResources.nodeWorldTransforms[nodeIndex];

        	const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
        	for (const fastgltf::Primitive &primitive : mesh.primitives){
        	    const auto &[indexBuffer, vertexBuffer] = sharedData->primitiveBuffers.at(&primitive);
		        sharedData->meshRenderer.draw(cb, indexBuffer, vertexBuffer, { nodeWorldTransform, projectionView, viewPosition });
        	}
        }

		dfs.pop();
		dfs.push_range(node.children | std::views::reverse);
	}

	// End dynamic rendering.
	cb.endRenderingKHR();

	// Change image layout to PresentSrcKHR.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		vk::ImageMemoryBarrier{
			vk::AccessFlagBits::eColorAttachmentWrite, {},
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			attachmentGroup.colorAttachments[0].image,
			vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
		});

	cb.end();
}