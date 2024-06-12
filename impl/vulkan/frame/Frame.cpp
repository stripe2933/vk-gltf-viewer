module;

#include <cstdint>
#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <ranges>
#include <stack>
#include <stdexcept>
#include <unordered_map>

#include <fastgltf/core.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.frame.Frame;

import :gltf.AssetResources;
import :gltf.SceneResources;
import :helpers.ranges;

vk_gltf_viewer::vulkan::Frame::Frame(
	const Gpu &gpu,
	const std::shared_ptr<SharedData> &sharedData
) : sharedData { sharedData },
	depthImage { createDepthImage(gpu.allocator) },
	depthPrepassAttachmentGroup { createDepthPrepassAttachmentGroup(gpu) },
	primaryAttachmentGroup { createPrimaryAttachmentGroup(gpu) },
    descriptorPool { createDescriptorPool(gpu.device) },
	graphicsCommandPool { createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent) },
	cameraBuffer { createCameraBuffer(gpu.allocator) },
	depthSets { *gpu.device, *descriptorPool, sharedData->depthRenderer.descriptorSetLayouts },
	primitiveSets { *gpu.device, *descriptorPool, sharedData->primitiveRenderer.descriptorSetLayouts },
    skyboxSets { *gpu.device, *descriptorPool, sharedData->skyboxRenderer.descriptorSetLayouts },
	depthPrepassFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} },
	drawFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	blitToSwapchainFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	inFlightFence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } } {
	// Update per-frame descriptor sets.
	gpu.device.updateDescriptorSets(
	    ranges::array_cat(
	    	depthSets.getDescriptorWrites0(
		    	{ sharedData->sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData->sceneResources.nodeTransformBuffer, 0, vk::WholeSize }).get(),
		    primitiveSets.getDescriptorWrites0(
		    	{ cameraBuffer, 0, vk::WholeSize },
		    	{ sharedData->cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize },
		    	*sharedData->prefilteredmapImageView,
		    	*sharedData->brdfmapImageView).get(),
		    primitiveSets.getDescriptorWrites1(
				sharedData->assetResources.textures,
		    	{ sharedData->assetResources.materialBuffer, 0, vk::WholeSize }).get(),
		    primitiveSets.getDescriptorWrites2(
		    	{ sharedData->sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData->sceneResources.nodeTransformBuffer, 0, vk::WholeSize }).get(),
		    skyboxSets.getDescriptorWrites0(*sharedData->cubemapImageView).get()),
		{});

	// Allocate per-frame command buffers.
	std::tie(depthPrepassCommandBuffer, drawCommandBuffer, blitToSwapchainCommandBuffer)
	    = (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	    	*graphicsCommandPool,
	    	vk::CommandBufferLevel::ePrimary,
	    	3,
	    })
		| ranges::to_array<3>();

	initAttachmentLayouts(gpu);
}

auto vk_gltf_viewer::vulkan::Frame::onLoop(
	const Gpu &gpu
) -> bool {
	constexpr std::uint64_t MAX_TIMEOUT = std::numeric_limits<std::uint64_t>::max();

	// Wait for the previous frame execution to finish.
	if (auto result = gpu.device.waitForFences(*inFlightFence, true, MAX_TIMEOUT); result != vk::Result::eSuccess) {
		throw std::runtime_error{ std::format("Failed to wait for in-flight fence: {}", to_string(result)) };
	}
	gpu.device.resetFences(*inFlightFence);

	// Update per-frame resources.
	update();

	// Acquire the next swapchain image.
	std::uint32_t imageIndex;
	try {
		imageIndex = (*gpu.device).acquireNextImageKHR(*sharedData->swapchain, MAX_TIMEOUT, *swapchainImageAcquireSema).value;
	}
	catch (const vk::OutOfDateKHRError&) {
		return false;
	}

	// Record commands.
	depthPrepass(depthPrepassCommandBuffer);
	draw(drawCommandBuffer);
	blitToSwapchain(blitToSwapchainCommandBuffer, sharedData->swapchainAttachmentGroups[imageIndex]);

	// Submit draw command to the graphics queue.
	constexpr std::array drawWaitStages {
		vku::toFlags(vk::PipelineStageFlagBits::eEarlyFragmentTests),
	};
	const std::array blitToSwapchainWaitSemas { *swapchainImageAcquireSema, *drawFinishSema };
	constexpr std::array blitToSwapchainWaitStages {
		vku::toFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput),
		vku::toFlags(vk::PipelineStageFlagBits::eTransfer),
	};
	gpu.queues.graphicsPresent.submit(std::array {
		vk::SubmitInfo {
			{},
			{},
			depthPrepassCommandBuffer,
			*depthPrepassFinishSema,
		},
		vk::SubmitInfo {
			*depthPrepassFinishSema,
			drawWaitStages,
			drawCommandBuffer,
			*drawFinishSema,
		},
		vk::SubmitInfo {
			blitToSwapchainWaitSemas,
			blitToSwapchainWaitStages,
			blitToSwapchainCommandBuffer,
			*blitToSwapchainFinishSema,
		},
	}, *inFlightFence);

	// Present the image to the swapchain.
	try {
		// The result codes VK_ERROR_OUT_OF_DATE_KHR and VK_SUBOPTIMAL_KHR have the same meaning when
		// returned by vkQueuePresentKHR as they do when returned by vkAcquireNextImageKHR.
		if (gpu.queues.graphicsPresent.presentKHR({ *blitToSwapchainFinishSema, *sharedData->swapchain, imageIndex }) == vk::Result::eSuboptimalKHR) {
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
	depthImage = createDepthImage(gpu.allocator);
	depthPrepassAttachmentGroup = createDepthPrepassAttachmentGroup(gpu);
	primaryAttachmentGroup = createPrimaryAttachmentGroup(gpu);
	initAttachmentLayouts(gpu);
}

auto vk_gltf_viewer::vulkan::Frame::createDepthImage(
	vma::Allocator allocator
) const -> decltype(depthImage) {
	return { allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eD32Sfloat,
		vk::Extent3D { sharedData->swapchainExtent, 1 },
		1, 1,
		vk::SampleCountFlagBits::e4,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eDepthStencilAttachment,
	}, vma::AllocationCreateInfo {
		{},
		vma::MemoryUsage::eAutoPreferDevice,
	} };
}

auto vk_gltf_viewer::vulkan::Frame::createDepthPrepassAttachmentGroup(
	const Gpu &gpu
) const -> decltype(depthPrepassAttachmentGroup) {
	vku::MsaaAttachmentGroup attachmentGroup { sharedData->swapchainExtent, vk::SampleCountFlagBits::e4 };
	attachmentGroup.setDepthAttachment(gpu.device, depthImage);
	return attachmentGroup;
}

auto vk_gltf_viewer::vulkan::Frame::createPrimaryAttachmentGroup(
	const Gpu &gpu
) const -> decltype(primaryAttachmentGroup) {
	vku::MsaaAttachmentGroup attachmentGroup { sharedData->swapchainExtent, vk::SampleCountFlagBits::e4 };
	attachmentGroup.addColorAttachment(
		gpu.device,
		attachmentGroup.storeImage(
			attachmentGroup.createColorImage(gpu.allocator, vk::Format::eR16G16B16A16Sfloat)),
		attachmentGroup.storeImage(
			attachmentGroup.createResolveImage(gpu.allocator, vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eTransferSrc)));
	attachmentGroup.setDepthAttachment(gpu.device, depthImage);
	return attachmentGroup;
}

auto vk_gltf_viewer::vulkan::Frame::createDescriptorPool(
    const vk::raii::Device &device
) const -> decltype(descriptorPool) {
    const std::array poolSizes {
    	vk::DescriptorPoolSize {
    		vk::DescriptorType::eCombinedImageSampler,
    		1 /* PrimitiveRenderer prefilteredmap */
    		+ 1 /* PrimitiveRenderer brdfmap */
    		+ static_cast<std::uint32_t>(sharedData->assetResources.textures.size()) /* PrimitiveRenderer textures */
    		+ 1 /* SkyboxRenderer cubemap */,
    	},
    	vk::DescriptorPoolSize {
    		vk::DescriptorType::eStorageBuffer,
			1 /* DepthRenderer primitiveBuffer */
			+ 1 /* DepthRenderer nodeTransformBuffer */
    		+ 1 /* PrimitiveRenderer materialBuffer */
    		+ 1 /* PrimitiveRenderer nodeTransformBuffer */
    		+ 1 /* PrimitiveRenderer primitiveBuffer */,
    	},
    	vk::DescriptorPoolSize {
    	    vk::DescriptorType::eUniformBuffer,
    		1 /* PrimitiveRenderer cameraBuffer */
    		+ 1 /* PrimitiveRenderer cubemapSphericalHarmonicsBuffer */,
    	},
    };
	return { device, vk::DescriptorPoolCreateInfo{
		vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
		1 /* DepthRenderer */
		+ 3 /* PrimitiveRenderer */
		+ 1 /* SkyboxRenderer */,
		poolSizes,
	} };
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

auto vk_gltf_viewer::vulkan::Frame::createCameraBuffer(
	vma::Allocator allocator
) const -> decltype(cameraBuffer) {
	return { allocator, vk::BufferCreateInfo {
		{},
		sizeof(pipelines::PrimitiveRenderer::Camera),
		vk::BufferUsageFlagBits::eUniformBuffer,
	}, vma::AllocationCreateInfo {
		vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
		vma::MemoryUsage::eAuto,
	} };
}

auto vk_gltf_viewer::vulkan::Frame::initAttachmentLayouts(
	const Gpu &gpu
) const -> void {
	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, {}, {},
			std::array {
				vk::ImageMemoryBarrier {
					{}, {},
					{}, vk::ImageLayout::eDepthAttachmentOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					depthImage, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth),
				},
				vk::ImageMemoryBarrier {
					{}, {},
					{}, vk::ImageLayout::eColorAttachmentOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					primaryAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
				},
				vk::ImageMemoryBarrier {
					{}, {},
					{}, vk::ImageLayout::eTransferSrcOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					primaryAttachmentGroup.colorAttachments[0].resolveImage, vku::fullSubresourceRange(),
				},
			});
	});
	gpu.queues.graphicsPresent.waitIdle();
}

// TODO: does this really need to be here? ^^;;;
float rotation = 0.f;
glm::mat4 view;
glm::mat4 projection;

auto vk_gltf_viewer::vulkan::Frame::update() -> void {
	rotation += 5e-3f;

	const auto viewPosition = glm::vec3 { 4.f * std::cos(rotation), 0.f, 4.f * std::sin(rotation) };
	view = glm::gtc::lookAt(viewPosition, glm::vec3{ 0.f }, glm::vec3{ 0.f, 1.f, 0.f });
	projection = glm::gtc::perspective(glm::radians(45.0f), vku::aspect(sharedData->swapchainExtent), 1e-2f, 1e2f);
	cameraBuffer.asValue<pipelines::PrimitiveRenderer::Camera>() = {
		projection * view,
		viewPosition,
	};
}

auto vk_gltf_viewer::vulkan::Frame::depthPrepass(
	vk::CommandBuffer cb
) const -> void {
	cb.begin(vk::CommandBufferBeginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	// Begin dynamic rendering.
	cb.beginRenderingKHR(depthPrepassAttachmentGroup.getRenderingInfo(
		{},
		std::tuple { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::ClearDepthStencilValue { 1.f, 0U } }));

	// Set viewport and scissor.
	depthPrepassAttachmentGroup.setViewport(cb, true);
	depthPrepassAttachmentGroup.setScissor(cb);

	sharedData->depthRenderer.bindPipeline(cb);
	sharedData->depthRenderer.bindDescriptorSets(cb, depthSets);
	sharedData->depthRenderer.pushConstants(cb, { .projectionView = projection * view });
	for (const auto &[criteria, indirectDrawCommandBuffer] : sharedData->sceneResources.indirectDrawCommandBuffers) {
		const vk::IndexType indexType = criteria.indexType.value();
		cb.bindIndexBuffer(sharedData->assetResources.indexBuffers.at(indexType), 0, indexType);
		cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
	}

	// End dynamic rendering.
	cb.endRenderingKHR();

	cb.end();
}

auto vk_gltf_viewer::vulkan::Frame::draw(
	vk::CommandBuffer cb
) const -> void {
	cb.begin(vk::CommandBufferBeginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	// Change image layout to ColorAttachmentOptimal.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{}, {}, {},
		vk::ImageMemoryBarrier{
			{}, vk::AccessFlagBits::eColorAttachmentWrite,
			vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			primaryAttachmentGroup.colorAttachments[0].resolveImage, vku::fullSubresourceRange(),
		});

	// Begin dynamic rendering.
	cb.beginRenderingKHR(primaryAttachmentGroup.getRenderingInfo(
		std::array {
			std::tuple { vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ClearColorValue{} },
		},
		std::tuple { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eDontCare, vk::ClearDepthStencilValue{} }));

	// Set viewport and scissor.
	primaryAttachmentGroup.setViewport(cb, true);
	primaryAttachmentGroup.setScissor(cb);

	// Draw glTF mesh.
	sharedData->primitiveRenderer.bindPipeline(cb);
	sharedData->primitiveRenderer.bindDescriptorSets(cb, primitiveSets);
	for (const auto &[criteria, indirectDrawCommandBuffer] : sharedData->sceneResources.indirectDrawCommandBuffers) {
		const vk::IndexType indexType = criteria.indexType.value();
		cb.bindIndexBuffer(sharedData->assetResources.indexBuffers.at(indexType), 0, indexType);
		cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
	}

	// Draw skybox.
	sharedData->skyboxRenderer.draw(cb, skyboxSets, { projection * glm::mat4 { glm::mat3 { view } } });

	// End dynamic rendering.
	cb.endRenderingKHR();

	// Change image layout to TrasferSrcOptimal.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		vk::ImageMemoryBarrier{
			vk::AccessFlagBits::eColorAttachmentWrite, {},
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			primaryAttachmentGroup.colorAttachments[0].resolveImage, vku::fullSubresourceRange(),
		});

	cb.end();
}

auto vk_gltf_viewer::vulkan::Frame::blitToSwapchain(
	vk::CommandBuffer cb,
	const vku::AttachmentGroup &swapchainAttachmentGroup
) const -> void {
	cb.begin(vk::CommandBufferBeginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	// Change image layout to TransferDstOptimal.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
		{}, {}, {},
		vk::ImageMemoryBarrier{
			{}, vk::AccessFlagBits::eTransferWrite,
			vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			swapchainAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
		});

	// Blit from primaryAttachmentGroup color attachment resolve image to swapchainAttachmentGroup color attachment image.
	cb.blitImage(
		primaryAttachmentGroup.colorAttachments[0].resolveImage, vk::ImageLayout::eTransferSrcOptimal,
		swapchainAttachmentGroup.colorAttachments[0].image, vk::ImageLayout::eTransferDstOptimal,
		vk::ImageBlit{
			{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
			{ vk::Offset3D{}, vk::Offset3D { static_cast<std::int32_t>(sharedData->swapchainExtent.width), static_cast<std::int32_t>(sharedData->swapchainExtent.height), 1 } },
			{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
			{ vk::Offset3D{}, vk::Offset3D { static_cast<std::int32_t>(sharedData->swapchainExtent.width), static_cast<std::int32_t>(sharedData->swapchainExtent.height), 1 } },
		},
		vk::Filter::eLinear);

	// Change image layout to PresentSrcKHR.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		vk::ImageMemoryBarrier{
			vk::AccessFlagBits::eTransferWrite, {},
			vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			swapchainAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
		});

	cb.end();
}