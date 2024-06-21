module;

#include <cstdint>
#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <tuple>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fastgltf/core.hpp>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.frame.Frame;

import :gltf.AssetResources;
import :gltf.SceneResources;
import :helpers.ranges;

constexpr auto NO_INDEX = std::numeric_limits<std::uint32_t>::max();

vk_gltf_viewer::vulkan::Frame::Frame(
	const std::shared_ptr<SharedData> &sharedData,
	const Gpu &gpu
) : sharedData { sharedData },
    gpu { gpu },
	hoveringNodeIndexBuffer {
		gpu.allocator,
		std::numeric_limits<std::uint32_t>::max(),
		vk::BufferUsageFlagBits::eTransferDst,
		vma::AllocationCreateInfo {
			vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
			vma::MemoryUsage::eAuto,
		},
	} {
	// Update per-frame descriptor sets.
	gpu.device.updateDescriptorSets(
	    ranges::array_cat(
	    	depthSets.getDescriptorWrites0(
		    	{ sharedData->sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData->sceneResources.nodeTransformBuffer, 0, vk::WholeSize }).get(),
		    primitiveSets.getDescriptorWrites0(
		    	{ sharedData->imageBasedLightingResources.value().cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize },
		    	*sharedData->imageBasedLightingResources.value().prefilteredmapImageView,
		    	*sharedData->brdfmapImageView).get(),
		    primitiveSets.getDescriptorWrites1(
				sharedData->assetResources.textures,
		    	{ sharedData->assetResources.materialBuffer.value(), 0, vk::WholeSize }).get(),
		    primitiveSets.getDescriptorWrites2(
		    	{ sharedData->sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData->sceneResources.nodeTransformBuffer, 0, vk::WholeSize }).get(),
		    skyboxSets.getDescriptorWrites0(*sharedData->imageBasedLightingResources.value().cubemapImageView).get()),
		{});

	// Allocate per-frame command buffers.
	std::tie(jumpFloodCommandBuffer)
	    = (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	    	*computeCommandPool,
	    	vk::CommandBufferLevel::ePrimary,
	    	1,
	    })
		| ranges::to_array<1>();
	std::tie(depthPrepassCommandBuffer, drawCommandBuffer, compositeCommandBuffer)
	    = (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	    	*graphicsCommandPool,
	    	vk::CommandBufferLevel::ePrimary,
	    	3,
	    })
		| ranges::to_array<3>();

	initAttachmentLayouts();
}

auto vk_gltf_viewer::vulkan::Frame::onLoop(
	const OnLoopTask &task
) -> std::expected<OnLoopResult, OnLoopError> {
	constexpr std::uint64_t MAX_TIMEOUT = std::numeric_limits<std::uint64_t>::max();

	// Wait for the previous frame execution to finish.
	if (auto result = gpu.device.waitForFences(*inFlightFence, true, MAX_TIMEOUT); result != vk::Result::eSuccess) {
		throw std::runtime_error{ std::format("Failed to wait for in-flight fence: {}", to_string(result)) };
	}
	gpu.device.resetFences(*inFlightFence);

	OnLoopResult result{};

	// Update per-frame resources.
	update(task, result);

	// Acquire the next swapchain image.
	std::uint32_t imageIndex;
	try {
		imageIndex = (*gpu.device).acquireNextImageKHR(*sharedData->swapchain, MAX_TIMEOUT, *swapchainImageAcquireSema).value;
	}
	catch (const vk::OutOfDateKHRError&) {
		return std::unexpected { OnLoopError::SwapchainAcquireFailed };
	}

	// Record commands.
	graphicsCommandPool.reset();
	computeCommandPool.reset();

	depthPrepassCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	recordDepthPrepassCommands(depthPrepassCommandBuffer, task);
	depthPrepassCommandBuffer.end();

	jumpFloodCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	const std::optional<bool> jumpFloodResultForward = recordJumpFloodCalculationCommands(jumpFloodCommandBuffer, task);
	jumpFloodCommandBuffer.end();
	if (jumpFloodResultForward.has_value()) {
		gpu.device.updateDescriptorSets(
			outlineSets.getDescriptorWrites0(*jumpFloodResultForward ? *jumpFloodPongImageView : *jumpFloodPingImageView).get(),
			{});
	}

	drawCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	recordGltfPrimitiveDrawCommands(drawCommandBuffer, task);
	drawCommandBuffer.end();

	compositeCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	recordPostCompositionCommands(compositeCommandBuffer, jumpFloodResultForward, imageIndex, task);
	compositeCommandBuffer.end();

	// Submit commands to the corresponding queues.
	gpu.queues.graphicsPresent.submit(vk::SubmitInfo {
		{},
		{},
		depthPrepassCommandBuffer,
		*depthPrepassFinishSema,
	});

	constexpr std::array jumpFloodWaitStages {
		vku::toFlags(vk::PipelineStageFlagBits::eComputeShader),
	};
	gpu.queues.compute.submit(vk::SubmitInfo {
		*depthPrepassFinishSema,
		jumpFloodWaitStages,
		jumpFloodCommandBuffer,
		*jumpFloodFinishSema,
	});

	const std::array compositeWaitSemas { *swapchainImageAcquireSema, *drawFinishSema, *jumpFloodFinishSema };
	constexpr std::array compositeWaitStages {
		vku::toFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput),
		vku::toFlags(vk::PipelineStageFlagBits::eFragmentShader),
		vku::toFlags(vk::PipelineStageFlagBits::eFragmentShader),
	};
	gpu.queues.graphicsPresent.submit(std::array {
		vk::SubmitInfo {
			{},
			{},
			drawCommandBuffer,
			*drawFinishSema,
		},
		vk::SubmitInfo {
			compositeWaitSemas,
			compositeWaitStages,
			compositeCommandBuffer,
			*compositeFinishSema,
		},
	}, *inFlightFence);

	// Present the image to the swapchain.
	try {
		// The result codes VK_ERROR_OUT_OF_DATE_KHR and VK_SUBOPTIMAL_KHR have the same meaning when
		// returned by vkQueuePresentKHR as they do when returned by vkAcquireNextImageKHR.
		if (gpu.queues.graphicsPresent.presentKHR({ *compositeFinishSema, *sharedData->swapchain, imageIndex }) == vk::Result::eSuboptimalKHR) {
			throw vk::OutOfDateKHRError { "Suboptimal swapchain" };
		}
		result.presentSuccess = true;
	}
	catch (const vk::OutOfDateKHRError&) {
		result.presentSuccess = false;
	}

	return std::move(result);
}

auto vk_gltf_viewer::vulkan::Frame::handleSwapchainResize(
	vk::SurfaceKHR surface,
	const vk::Extent2D &newExtent
) -> void {

}

auto vk_gltf_viewer::vulkan::Frame::createJumpFloodImage(
	const vk::Extent2D &extent
) const -> decltype(jumpFloodImage) {
	return std::make_unique<vku::AllocatedImage>(
		gpu.allocator,
		vk::ImageCreateInfo {
			{},
			vk::ImageType::e2D,
			vk::Format::eR16G16B16A16Uint,
			vk::Extent3D { extent, 1 },
			1, 2, // arrayLevels=0 for ping image, arrayLevels=1 for pong image.
			vk::SampleCountFlagBits::e1,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment /* write from DepthRenderer */
				| vk::ImageUsageFlagBits::eStorage /* used as ping pong image in JumpFloodComputer, read from OutlineRenderer */,
		},
		vma::AllocationCreateInfo {
			{},
			vma::MemoryUsage::eAutoPreferDevice,
		});
}

auto vk_gltf_viewer::vulkan::Frame::createJumpFloodImageView(
    std::uint32_t arrayLayer
) const -> std::unique_ptr<vk::raii::ImageView> {
	return std::make_unique<vk::raii::ImageView>(
		gpu.device,
		vk::ImageViewCreateInfo {
			{},
			*jumpFloodImage,
			vk::ImageViewType::e2D,
			jumpFloodImage->format,
			{},
			vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, arrayLayer, 1 },
		});
}

auto vk_gltf_viewer::vulkan::Frame::createDepthPrepassAttachmentGroup(
    const vk::Extent2D &extent
) const -> decltype(depthPrepassAttachmentGroup) {
	auto attachmentGroup = std::make_unique<vku::AttachmentGroup>(extent);
	attachmentGroup->addColorAttachment(
		gpu.device,
		attachmentGroup->storeImage(
			attachmentGroup->createColorImage(gpu.allocator, vk::Format::eR32Uint, vk::ImageUsageFlagBits::eTransferSrc)));
	attachmentGroup->addColorAttachment(
		gpu.device,
		*jumpFloodImage, jumpFloodImage->format,
		{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image subresource */);
	attachmentGroup->setDepthAttachment(
		gpu.device,
		attachmentGroup->storeImage(
			attachmentGroup->createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
	return attachmentGroup;
}

auto vk_gltf_viewer::vulkan::Frame::createPrimaryAttachmentGroup(
    const vk::Extent2D &extent
) const -> decltype(primaryAttachmentGroup) {
	auto attachmentGroup = std::make_unique<vku::MsaaAttachmentGroup>(extent, vk::SampleCountFlagBits::e4);
	attachmentGroup->addColorAttachment(
		gpu.device,
		attachmentGroup->storeImage(
			attachmentGroup->createColorImage(gpu.allocator, vk::Format::eR16G16B16A16Sfloat)),
		attachmentGroup->storeImage(
			attachmentGroup->createResolveImage(gpu.allocator, vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eStorage)));
	attachmentGroup->setDepthAttachment(
		gpu.device,
		attachmentGroup->storeImage(
			attachmentGroup->createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
	return attachmentGroup;
}

auto vk_gltf_viewer::vulkan::Frame::createDescriptorPool() const -> decltype(descriptorPool) {
	return {
		gpu.device,
		(vku::PoolSizes { sharedData->depthRenderer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData->jumpFloodComputer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData->primitiveRenderer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData->skyboxRenderer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData->outlineRenderer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData->rec709Renderer.descriptorSetLayouts })
		.getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind)
	};
}

auto vk_gltf_viewer::vulkan::Frame::createCommandPool(
	std::uint32_t queueFamilyIndex
) const -> vk::raii::CommandPool {
	return { gpu.device, vk::CommandPoolCreateInfo{
		{},
		queueFamilyIndex,
	} };
}

auto vk_gltf_viewer::vulkan::Frame::initAttachmentLayouts() const -> void {
	std::vector<vk::ImageMemoryBarrier> barriers;
	const auto appendBarrier = [&](vk::ImageLayout newLayout, vk::Image image, const vk::ImageSubresourceRange &subresourceRange = vku::fullSubresourceRange()) {
		barriers.emplace_back(
			vk::AccessFlags{}, vk::AccessFlags{},
			vk::ImageLayout::eUndefined, newLayout,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			image, subresourceRange);
	};

	if (passthruImageInitialized) {
		appendBarrier(vk::ImageLayout::eGeneral, *jumpFloodImage);
		appendBarrier(vk::ImageLayout::eTransferSrcOptimal, depthPrepassAttachmentGroup->colorAttachments[0].image);
		appendBarrier(vk::ImageLayout::eDepthAttachmentOptimal, depthPrepassAttachmentGroup->depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth));
		appendBarrier(vk::ImageLayout::eColorAttachmentOptimal, primaryAttachmentGroup->colorAttachments[0].image);
		appendBarrier(vk::ImageLayout::eGeneral, primaryAttachmentGroup->colorAttachments[0].resolveImage);
		appendBarrier(vk::ImageLayout::eDepthAttachmentOptimal, primaryAttachmentGroup->depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth));
	}

	if (!barriers.empty()) {
		vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
				{}, {}, {}, barriers);
		});
		gpu.queues.graphicsPresent.waitIdle();
	}
}

auto vk_gltf_viewer::vulkan::Frame::update(
    const OnLoopTask &task,
	OnLoopResult &result
) -> void {
	// Get node index under the cursor from hoveringNodeIndexBuffer.
	// If it is not NO_INDEX (i.e. node index is found), update hoveringNodeIndex.
	if (auto value = std::exchange(hoveringNodeIndexBuffer.asValue<std::uint32_t>(), NO_INDEX); value != NO_INDEX) {
		result.hoveringNodeIndex = value;
	}

	// If passthru extent is different from current, dependent images have to be recreated.
	if (!passthruImageInitialized || vku::toExtent2D(jumpFloodImage->extent) != task.passthruRect.extent) {
		jumpFloodImage = createJumpFloodImage(task.passthruRect.extent);
		jumpFloodPingImageView = createJumpFloodImageView(0);
		jumpFloodPongImageView = createJumpFloodImageView(1);
		depthPrepassAttachmentGroup = createDepthPrepassAttachmentGroup(task.passthruRect.extent);
		primaryAttachmentGroup = createPrimaryAttachmentGroup(task.passthruRect.extent);
		passthruImageInitialized = true;

		initAttachmentLayouts();
		gpu.device.updateDescriptorSets(
			ranges::array_cat(
				jumpFloodSets.getDescriptorWrites0(**jumpFloodPingImageView, **jumpFloodPongImageView).get(),
				rec709Sets.getDescriptorWrites0(*primaryAttachmentGroup->colorAttachments[0].resolveView).get()),
			{});
	}
}

auto vk_gltf_viewer::vulkan::Frame::recordDepthPrepassCommands(
	vk::CommandBuffer cb,
	const OnLoopTask &task
) const -> void {
	// Change color attachment layout to ColorAttachmentOptimal.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{}, {}, {},
		std::array {
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				depthPrepassAttachmentGroup->colorAttachments[0].image, vku::fullSubresourceRange(),
			},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageLayout::eGeneral, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				*jumpFloodImage,
				{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
			},
		});

	cb.beginRenderingKHR(depthPrepassAttachmentGroup->getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { std::numeric_limits<std::uint32_t>::max(), 0U, 0U, 0U } },
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
		},
		vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 1.f, 0U } }));

	depthPrepassAttachmentGroup->setViewport(cb, true);
	depthPrepassAttachmentGroup->setScissor(cb);

	sharedData->depthRenderer.bindPipeline(cb);
	sharedData->depthRenderer.bindDescriptorSets(cb, depthSets);
	sharedData->depthRenderer.pushConstants(cb, {
		task.camera.projection * task.camera.view,
		task.hoveringNodeIndex.value_or(NO_INDEX),
		task.selectedNodeIndex.value_or(NO_INDEX),
	});
	for (const auto &[criteria, indirectDrawCommandBuffer] : sharedData->sceneResources.indirectDrawCommandBuffers) {
		if (const auto &indexType = criteria.indexType) {
			cb.bindIndexBuffer(sharedData->assetResources.indexBuffers.at(*indexType), 0, *indexType);
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}

	cb.endRenderingKHR();

	const std::array imageMemoryBarriers {
		// For copying to hoveringNodeIndexBuffer.
		vk::ImageMemoryBarrier2 {
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			depthPrepassAttachmentGroup->colorAttachments[0].image, vku::fullSubresourceRange(),
		},
		// Release jump flood resources' ping images queue family ownership.
		vk::ImageMemoryBarrier2 {
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
			{}, {},
			gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
			*jumpFloodImage,
			{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
		},
	};
	cb.pipelineBarrier2KHR({
		{},
		{}, {}, imageMemoryBarriers,
	});

	const auto cursorPosFromPassthruRectTopLeft
		= task.mouseCursorOffset.transform([&](const vk::Offset2D &offset) {
			return vk::Offset2D { offset.x - task.passthruRect.offset.x, offset.y - task.passthruRect.offset.y };
		});
	const auto isCursorInPassthruRect
		= cursorPosFromPassthruRectTopLeft.transform([&](const vk::Offset2D &offset) {
			return 0 <= offset.x && offset.x < task.passthruRect.extent.width
			    && 0 <= offset.y && offset.y < task.passthruRect.extent.height;
		});
	if (isCursorInPassthruRect.value_or(false)) {
		cb.copyImageToBuffer(
			depthPrepassAttachmentGroup->colorAttachments[0].image, vk::ImageLayout::eTransferSrcOptimal,
			hoveringNodeIndexBuffer,
			vk::BufferImageCopy {
				0, {}, {},
				{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
				vk::Offset3D { *cursorPosFromPassthruRectTopLeft, 0 },
				{ 1, 1, 1 },
			});
	}
}

auto vk_gltf_viewer::vulkan::Frame::recordJumpFloodCalculationCommands(
	vk::CommandBuffer cb,
	const OnLoopTask &task
) const -> std::optional<bool> {
	// Change image layout and acquire queue family ownership.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			{}, vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
		    gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
		    *jumpFloodImage,
		    { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping */,
		});

	if (task.hoveringNodeIndex || task.selectedNodeIndex) {
		auto forward = sharedData->jumpFloodComputer.compute(
			cb, jumpFloodSets, vku::toExtent2D(jumpFloodImage->extent));
		// Release queue family ownership.
		if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eAllCommands,
				{}, {}, {},
				vk::ImageMemoryBarrier {
					vk::AccessFlagBits::eShaderWrite, {},
					{}, {},
					gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
					*jumpFloodImage, { vk::ImageAspectFlagBits::eColor, 0, 1, forward, 1 },
				});
		}
		return forward;
	}
	return std::nullopt;
}

auto vk_gltf_viewer::vulkan::Frame::recordGltfPrimitiveDrawCommands(
	vk::CommandBuffer cb,
	const OnLoopTask &task
) const -> void {
	// Change resolve image layout from General to ColorAttachmentOptimal.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			{}, vk::AccessFlagBits::eColorAttachmentWrite,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			primaryAttachmentGroup->colorAttachments[0].resolveImage, vku::fullSubresourceRange(),
		});

	cb.beginRenderingKHR(primaryAttachmentGroup->getRenderingInfo(
		std::array {
			vku::MsaaAttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare },
		},
		vku::MsaaAttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 1.f, 0U } }));

	primaryAttachmentGroup->setViewport(cb, true);
	primaryAttachmentGroup->setScissor(cb);

	// Draw glTF mesh.
	sharedData->primitiveRenderer.bindPipeline(cb);
	sharedData->primitiveRenderer.bindDescriptorSets(cb, primitiveSets);
	sharedData->primitiveRenderer.pushConstants(cb, { task.camera.projection * task.camera.view, inverse(task.camera.view)[3] });
	for (const auto &[criteria, indirectDrawCommandBuffer] : sharedData->sceneResources.indirectDrawCommandBuffers) {
		cb.setCullMode(criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

		if (const auto &indexType = criteria.indexType) {
			cb.bindIndexBuffer(sharedData->assetResources.indexBuffers.at(*indexType), 0, *indexType);
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}

	// Draw skybox.
	const glm::mat4 noTranslationView = { glm::mat3 { task.camera.view } };
	sharedData->skyboxRenderer.draw(cb, skyboxSets, {
		task.camera.projection * noTranslationView,
	});

	cb.endRenderingKHR();
}

auto vk_gltf_viewer::vulkan::Frame::recordPostCompositionCommands(
	vk::CommandBuffer cb,
	std::optional<bool> isJumpFloodResultForward,
	std::uint32_t swapchainImageIndex,
	const OnLoopTask &task
) const -> void {
	std::vector memoryBarriers {
		// Change PrimaryAttachmentGroup's resolve image layout from ColorAttachmentOptimal to General.
		vk::ImageMemoryBarrier2 {
			{}, {},
			vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderStorageRead,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			primaryAttachmentGroup->colorAttachments[0].resolveImage, vku::fullSubresourceRange(),
		},
		// Change swapchain image layout from PresentSrcKHR to ColorAttachmentOptimal.
		vk::ImageMemoryBarrier2 {
			{}, {},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			sharedData->swapchainImages[swapchainImageIndex], vku::fullSubresourceRange(),
		},
	};
	// Acquire jumpFloodImage queue family ownership, if necessary.
	if ((task.hoveringNodeIndex || task.selectedNodeIndex) && gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
		memoryBarriers.emplace_back(
			vk::PipelineStageFlags2{}, vk::AccessFlags2{},
			vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderStorageRead,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
			gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
			*jumpFloodImage,
			vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, *isJumpFloodResultForward, 1 });
	}
	cb.pipelineBarrier2KHR({
		{},
		{}, {}, memoryBarriers,
	});

	const vk::Viewport passthruViewport {
		// Use negative viewport.
		static_cast<float>(task.passthruRect.offset.x), static_cast<float>(task.passthruRect.offset.y + task.passthruRect.extent.height),
		static_cast<float>(task.passthruRect.extent.width), -static_cast<float>(task.passthruRect.extent.height),
		0.f, 1.f,
	};
	const vk::Rect2D passthruScissor = task.passthruRect;

	// TODO: The primitive rendering image should be composited before outlining the hovered or selected nodes.
	//	Currently, this is achieved through two separate dynamic renderings, each with a consequent load operation set
	//	to ‘Load’. However, if Vulkan extensions such as VK_KHR_dynamic_rendering_local_read are available, a pipeline
	//	barrier can be inserted within the dynamic rendering scope, thus avoiding the need for duplicated rendering.

	// Start dynamic rendering with B8G8R8A8_SRGB format.
	cb.beginRenderingKHR(sharedData->swapchainAttachmentGroups[swapchainImageIndex].getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::ClearColorValue { 0.f, 0.f, 0.f, 0.f } }
		}));

	// Set viewport and scissor.
	cb.setViewport(0, passthruViewport);
	cb.setScissor(0, passthruScissor);

	// Draw primitive rendering image to swapchain, with Rec709 tone mapping.
	sharedData->rec709Renderer.draw(cb, rec709Sets, task.passthruRect.offset);

	cb.endRenderingKHR();

	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{},
		vk::MemoryBarrier {
			vk::AccessFlagBits::eColorAttachmentWrite,
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
		},
		{}, {});

	cb.beginRenderingKHR(sharedData->swapchainAttachmentGroups[swapchainImageIndex].getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore }
		}));

	// Draw hovering/selected node outline if exists.
	if (task.hoveringNodeIndex || task.selectedNodeIndex) {
		sharedData->outlineRenderer.bindPipeline(cb);
		sharedData->outlineRenderer.bindDescriptorSets(cb, outlineSets);

		if (task.selectedNodeIndex) {
			sharedData->outlineRenderer.pushConstants(cb, {
				.outlineColor = { 0.f, 1.f, 0.2f },
				.outlineThickness = 4.f,
				.passthruOffset = { task.passthruRect.offset.x, task.passthruRect.offset.y },
				.useZwComponent = true,
			});
			sharedData->outlineRenderer.draw(cb);
		}
		if (task.hoveringNodeIndex &&
			// If both selectedNodeIndex and hoveringNodeIndex exist and are the same, the outlines will overlap, so
			// the latter one doesn’t need to be rendered.
			(!task.selectedNodeIndex || *task.selectedNodeIndex != *task.hoveringNodeIndex)) {
			if (task.selectedNodeIndex) {
				// TODO: pipeline barrier required but because of the reason explained in above, it is not implemented.
				//  Implement it when available.
			}

			sharedData->outlineRenderer.pushConstants(cb, {
				.outlineColor = { 1.f, 0.5f, 0.2f },
				.outlineThickness = 4.f,
				.passthruOffset = { task.passthruRect.offset.x, task.passthruRect.offset.y },
				.useZwComponent = false,
			});
			sharedData->outlineRenderer.draw(cb);
		}
	}

    cb.endRenderingKHR();

	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{},
		vk::MemoryBarrier {
			vk::AccessFlagBits::eColorAttachmentWrite,
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
		},
		{}, {});

	// Start dynamic rendering with B8G8R8A8_UNORM format.
	cb.beginRenderingKHR(sharedData->imGuiSwapchainAttachmentGroups[swapchainImageIndex].getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore }
		}));

	// Draw ImGui.
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

    cb.endRenderingKHR();

	// Change swapchain image layout from ColorAttachmentOptimal to PresentSrcKHR.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			vk::AccessFlagBits::eColorAttachmentWrite, {},
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			sharedData->swapchainImages[swapchainImageIndex], vku::fullSubresourceRange(),
		});
}