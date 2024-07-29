module;

#include <fastgltf/core.hpp>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.Frame;

import std;
import :gltf.AssetResources;
import :gltf.SceneResources;
import :helpers.ranges;

constexpr auto NO_INDEX = std::numeric_limits<std::uint32_t>::max();

vk_gltf_viewer::vulkan::Frame::Frame(
	const Gpu &gpu,
	const SharedData &sharedData
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
		    alphaMaskedDepthSets.getDescriptorWrites0(
		    	{ *sharedData.primitiveRenderer.sampler, *sharedData.gltfFallbackImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
				sharedData.assetResources.textures,
		    	{ sharedData.assetResources.materialBuffer.value(), 0, vk::WholeSize }).get(),
		    alphaMaskedDepthSets.getDescriptorWrites1(
		    	{ sharedData.sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData.sceneResources.nodeTransformBuffer, 0, vk::WholeSize }),
		    depthSets.getDescriptorWrites0(
		    	{ sharedData.sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData.sceneResources.nodeTransformBuffer, 0, vk::WholeSize }),
		    primitiveSets.getDescriptorWrites0(
		    	{ sharedData.imageBasedLightingResources.value().cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize },
		    	*sharedData.imageBasedLightingResources.value().prefilteredmapImageView,
		    	*sharedData.brdfmapImageView).get(),
		    primitiveSets.getDescriptorWrites1(
                { *sharedData.primitiveRenderer.sampler, *sharedData.gltfFallbackImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
				sharedData.assetResources.textures,
		    	{ sharedData.assetResources.materialBuffer.value(), 0, vk::WholeSize }).get(),
		    primitiveSets.getDescriptorWrites2(
		    	{ sharedData.sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData.sceneResources.nodeTransformBuffer, 0, vk::WholeSize }),
		    skyboxSets.getDescriptorWrites0(*sharedData.imageBasedLightingResources.value().cubemapImageView).get(),
		    sphericalHarmonicsSets.getDescriptorWrites0(
		    	{ sharedData.imageBasedLightingResources.value().cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize })),
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

	if (task.swapchainResizeHandleInfo) {
		handleSwapchainResize(task.swapchainResizeHandleInfo->first, task.swapchainResizeHandleInfo->second);
	}

	// Update per-frame resources.
	update(task, result);

	// Acquire the next swapchain image.
	std::uint32_t imageIndex;
	try {
		imageIndex = (*gpu.device).acquireNextImageKHR(*sharedData.swapchain, MAX_TIMEOUT, *swapchainImageAcquireSema).value;
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

	// TODO: If there are multiple compute queues, distribute the tasks to avoid the compute pipeline stalling.
	std::optional<bool> hoveringNodeJumpFloodForward{}, selectedNodeJumpFloodForward{};
	jumpFloodCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	if (task.hoveringNodeIndex && task.hoveringNodeOutline) {
		hoveringNodeJumpFloodForward = recordJumpFloodComputeCommands(
			jumpFloodCommandBuffer,
			passthruExtentDependentResources->hoveringNodeOutlineJumpFloodResources.image,
			hoveringNodeJumpFloodSets,
			std::min(
				std::bit_ceil(static_cast<std::uint32_t>(task.hoveringNodeOutline->thickness)),
				std::max(passthruExtentDependentResources->extent.width, passthruExtentDependentResources->extent.height)));
		gpu.device.updateDescriptorSets(
			hoveringNodeOutlineSets.getDescriptorWrites0(
				*hoveringNodeJumpFloodForward
					? *passthruExtentDependentResources->hoveringNodeOutlineJumpFloodResources.pingImageView
					: *passthruExtentDependentResources->hoveringNodeOutlineJumpFloodResources.pongImageView).get(),
			{});
	}
	if (task.selectedNodeIndex && task.selectedNodeOutline) {
		selectedNodeJumpFloodForward = recordJumpFloodComputeCommands(
		jumpFloodCommandBuffer,
		passthruExtentDependentResources->selectedNodeOutlineJumpFloodResources.image,
		selectedNodeJumpFloodSets,
			std::min(
				std::bit_ceil(static_cast<std::uint32_t>(task.selectedNodeOutline->thickness)),
				std::max(passthruExtentDependentResources->extent.width, passthruExtentDependentResources->extent.height)));
		gpu.device.updateDescriptorSets(
			selectedNodeOutlineSets.getDescriptorWrites0(
				*selectedNodeJumpFloodForward
					? *passthruExtentDependentResources->selectedNodeOutlineJumpFloodResources.pingImageView
					: *passthruExtentDependentResources->selectedNodeOutlineJumpFloodResources.pongImageView).get(),
			{});
	}
	jumpFloodCommandBuffer.end();

	drawCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	recordGltfPrimitiveDrawCommands(drawCommandBuffer, task);
	drawCommandBuffer.end();

	compositeCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	recordPostCompositionCommands(compositeCommandBuffer, hoveringNodeJumpFloodForward, selectedNodeJumpFloodForward, imageIndex, task);
	compositeCommandBuffer.end();

	// Submit commands to the corresponding queues.
	gpu.queues.graphicsPresent.submit(vk::SubmitInfo {
		{},
		{},
		depthPrepassCommandBuffer,
		*depthPrepassFinishSema,
	});

	gpu.queues.compute.submit(vk::SubmitInfo {
		*depthPrepassFinishSema,
		vku::unsafeProxy({
			vk::Flags { vk::PipelineStageFlagBits::eComputeShader },
		}),
		jumpFloodCommandBuffer,
		*jumpFloodFinishSema,
	});

	gpu.queues.graphicsPresent.submit({
		vk::SubmitInfo {
			{},
			{},
			drawCommandBuffer,
			*drawFinishSema,
		},
		vk::SubmitInfo {
			vku::unsafeProxy({ *swapchainImageAcquireSema, *drawFinishSema, *jumpFloodFinishSema }),
			vku::unsafeProxy({
				vk::Flags { vk::PipelineStageFlagBits::eColorAttachmentOutput },
				vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
				vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
			}),
			compositeCommandBuffer,
			*compositeFinishSema,
		},
	}, *inFlightFence);

	// Present the image to the swapchain.
	try {
		// The result codes VK_ERROR_OUT_OF_DATE_KHR and VK_SUBOPTIMAL_KHR have the same meaning when
		// returned by vkQueuePresentKHR as they do when returned by vkAcquireNextImageKHR.
		if (gpu.queues.graphicsPresent.presentKHR({ *compositeFinishSema, *sharedData.swapchain, imageIndex }) == vk::Result::eSuboptimalKHR) {
			throw vk::OutOfDateKHRError { "Suboptimal swapchain" };
		}
		result.presentSuccess = true;
	}
	catch (const vk::OutOfDateKHRError&) {
		result.presentSuccess = false;
	}

	return std::move(result);
}

vk_gltf_viewer::vulkan::Frame::JumpFloodResources::JumpFloodResources(
	const Gpu &gpu,
	const vk::Extent2D &extent
) : image { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eR16G16Uint,
		vk::Extent3D { extent, 1 },
		1, 2, // arrayLevels=0 for ping image, arrayLevels=1 for pong image.
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eColorAttachment /* write from DepthRenderer */ | vk::ImageUsageFlagBits::eStorage /* used as ping pong image in JumpFloodComputer | vk::ImageUsageFlagBits:read from OutlineRenderer */,
	} },
	pingImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) },
	pongImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 }) } { }

auto vk_gltf_viewer::vulkan::Frame::handleSwapchainResize(
	vk::SurfaceKHR surface,
	const vk::Extent2D &newExtent
) -> void {

}

vk_gltf_viewer::vulkan::Frame::PassthruExtentDependentResources::PassthruExtentDependentResources(
	const Gpu &gpu,
	const vk::Extent2D &extent,
	vk::CommandBuffer graphicsCommandBuffer
) : extent { extent },
	hoveringNodeOutlineJumpFloodResources { gpu, extent },
	selectedNodeOutlineJumpFloodResources { gpu, extent },
	depthPrepassAttachmentGroup { gpu, hoveringNodeOutlineJumpFloodResources.image, selectedNodeOutlineJumpFloodResources.image, extent },
	primaryAttachmentGroup { gpu, extent } {
	recordInitialImageLayoutTransitionCommands(graphicsCommandBuffer);
}

auto vk_gltf_viewer::vulkan::Frame::PassthruExtentDependentResources::recordInitialImageLayoutTransitionCommands(
    vk::CommandBuffer graphicsCommandBuffer
) const -> void {
	const auto layoutTransitionBarrier = [](
		vk::ImageLayout newLayout,
		vk::Image image,
		const vk::ImageSubresourceRange &subresourceRange = vku::fullSubresourceRange()
	) {
		return vk::ImageMemoryBarrier {
			{}, {},
			{}, newLayout,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			image, subresourceRange
		};
	};
	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		{
			layoutTransitionBarrier(vk::ImageLayout::eGeneral, hoveringNodeOutlineJumpFloodResources.image),
			layoutTransitionBarrier(vk::ImageLayout::eGeneral, selectedNodeOutlineJumpFloodResources.image),
			layoutTransitionBarrier(vk::ImageLayout::eTransferSrcOptimal, depthPrepassAttachmentGroup.colorAttachments[0].image),
			layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, depthPrepassAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
			layoutTransitionBarrier(vk::ImageLayout::eColorAttachmentOptimal, primaryAttachmentGroup.colorAttachments[0].image),
			layoutTransitionBarrier(vk::ImageLayout::eGeneral, primaryAttachmentGroup.colorAttachments[0].resolveImage),
			layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, primaryAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
		});
}

auto vk_gltf_viewer::vulkan::Frame::createDescriptorPool() const -> decltype(descriptorPool) {
	return {
		gpu.device,
		(vku::PoolSizes { sharedData.alphaMaskedDepthRenderer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData.depthRenderer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData.jumpFloodComputer.descriptorSetLayouts } * 2
		    + vku::PoolSizes { sharedData.outlineRenderer.descriptorSetLayouts } * 2
		    + vku::PoolSizes { sharedData.primitiveRenderer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData.rec709Renderer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData.skyboxRenderer.descriptorSetLayouts }
		    + vku::PoolSizes { sharedData.sphericalHarmonicsRenderer.descriptorSetLayouts })
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

auto vk_gltf_viewer::vulkan::Frame::update(
    const OnLoopTask &task,
	OnLoopResult &result
) -> void {
	// Get node index under the cursor from hoveringNodeIndexBuffer.
	// If it is not NO_INDEX (i.e. node index is found), update hoveringNodeIndex.
	if (auto value = std::exchange(hoveringNodeIndexBuffer.asValue<std::uint32_t>(), NO_INDEX); value != NO_INDEX) {
		result.hoveringNodeIndex = value;
	}

	// If passthru extent is different from the current's, dependent images have to be recreated.
	if (!passthruExtentDependentResources || passthruExtentDependentResources->extent != task.passthruRect.extent) {
		vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
			passthruExtentDependentResources = PassthruExtentDependentResources { gpu, task.passthruRect.extent, cb };
		});
		gpu.queues.graphicsPresent.waitIdle(); // TODO: idling while frame execution is very inefficient.

		gpu.device.updateDescriptorSets(
			ranges::array_cat(
				hoveringNodeJumpFloodSets.getDescriptorWrites0(
					*passthruExtentDependentResources->hoveringNodeOutlineJumpFloodResources.pingImageView,
					*passthruExtentDependentResources->hoveringNodeOutlineJumpFloodResources.pongImageView).get(),
				selectedNodeJumpFloodSets.getDescriptorWrites0(
					*passthruExtentDependentResources->selectedNodeOutlineJumpFloodResources.pingImageView,
					*passthruExtentDependentResources->selectedNodeOutlineJumpFloodResources.pongImageView).get(),
				rec709Sets.getDescriptorWrites0(
					*passthruExtentDependentResources->primaryAttachmentGroup.colorAttachments[0].resolveView).get()),
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
		{
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				passthruExtentDependentResources->depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
			},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageLayout::eGeneral, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				passthruExtentDependentResources->hoveringNodeOutlineJumpFloodResources.image,
				{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
			},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageLayout::eGeneral, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				passthruExtentDependentResources->selectedNodeOutlineJumpFloodResources.image,
				{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
			},
		});

	cb.beginRenderingKHR(passthruExtentDependentResources->depthPrepassAttachmentGroup.getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { std::numeric_limits<std::uint32_t>::max(), 0U, 0U, 0U } },
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
		},
		vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0.f, 0U } }));

	cb.setViewport(0, passthruExtentDependentResources->depthPrepassAttachmentGroup.getViewport(true));
	cb.setScissor(0, passthruExtentDependentResources->depthPrepassAttachmentGroup.getScissor());

	// Render alphaMode=Opaque meshes.
	bool depthRendererBound = false;
	for (auto [begin, end] = sharedData.sceneResources.indirectDrawCommandBuffers.equal_range(fastgltf::AlphaMode::Opaque);
		 const auto &[criteria, indirectDrawCommandBuffer] : std::ranges::subrange(begin, end)) {
		if (!depthRendererBound) {
			sharedData.depthRenderer.bindPipeline(cb);
			sharedData.depthRenderer.bindDescriptorSets(cb, depthSets);
			sharedData.depthRenderer.pushConstants(cb, {
				task.camera.projection * task.camera.view,
				task.hoveringNodeIndex.value_or(NO_INDEX),
				task.selectedNodeIndex.value_or(NO_INDEX),
			});

			depthRendererBound = true;
		}

		cb.setCullMode(criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

		if (const auto &indexType = criteria.indexType) {
			cb.bindIndexBuffer(sharedData.assetResources.indexBuffers.at(*indexType), 0, *indexType);
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}

	// Render alphaMode=Mask meshes.
	bool alphaMaskedDepthRendererBound = false;
	for (auto [begin, end] = sharedData.sceneResources.indirectDrawCommandBuffers.equal_range(fastgltf::AlphaMode::Mask);
		 const auto &[criteria, indirectDrawCommandBuffer] : std::ranges::subrange(begin, end)) {
		if (!alphaMaskedDepthRendererBound) {
			sharedData.alphaMaskedDepthRenderer.bindPipeline(cb);
			sharedData.alphaMaskedDepthRenderer.bindDescriptorSets(cb, alphaMaskedDepthSets);
			sharedData.alphaMaskedDepthRenderer.pushConstants(cb, {
				task.camera.projection * task.camera.view,
				task.hoveringNodeIndex.value_or(NO_INDEX),
				task.selectedNodeIndex.value_or(NO_INDEX),
			});

			alphaMaskedDepthRendererBound = true;
		}

		cb.setCullMode(criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

		if (const auto &indexType = criteria.indexType) {
			cb.bindIndexBuffer(sharedData.assetResources.indexBuffers.at(*indexType), 0, *indexType);
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}

	// TODO: render alphaMode=Blend meshes.

	cb.endRenderingKHR();

	std::array imageMemoryBarriers {
		// For copying to hoveringNodeIndexBuffer.
		vk::ImageMemoryBarrier2 {
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			passthruExtentDependentResources->depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
		},
		// Change image layout from ColorAttachmentOptimal to General.
		vk::ImageMemoryBarrier2 {
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eAllCommands, {},
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			passthruExtentDependentResources->hoveringNodeOutlineJumpFloodResources.image,
			{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */
		},
		vk::ImageMemoryBarrier2 {
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eAllCommands, {},
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			passthruExtentDependentResources->selectedNodeOutlineJumpFloodResources.image,
			{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */
		},
	};
	// If graphics and compute queue families are different, queue family ownership have to be transferred.
	if (gpu.queueFamilies.graphicsPresent != gpu.queueFamilies.compute) {
		if (task.hoveringNodeIndex && task.hoveringNodeOutline) {
			get<1>(imageMemoryBarriers).srcQueueFamilyIndex = gpu.queueFamilies.graphicsPresent;
			get<1>(imageMemoryBarriers).dstQueueFamilyIndex = gpu.queueFamilies.compute;
		}
		if (task.selectedNodeIndex && task.selectedNodeOutline) {
			get<2>(imageMemoryBarriers).srcQueueFamilyIndex = gpu.queueFamilies.graphicsPresent;
			get<2>(imageMemoryBarriers).dstQueueFamilyIndex = gpu.queueFamilies.compute;
		}
	}
	cb.pipelineBarrier2KHR({
		{},
		{}, {}, imageMemoryBarriers
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
			passthruExtentDependentResources->depthPrepassAttachmentGroup.colorAttachments[0].image, vk::ImageLayout::eTransferSrcOptimal,
			hoveringNodeIndexBuffer,
			vk::BufferImageCopy {
				0, {}, {},
				{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
				vk::Offset3D { *cursorPosFromPassthruRectTopLeft, 0 },
				{ 1, 1, 1 },
			});
	}
}

auto vk_gltf_viewer::vulkan::Frame::recordJumpFloodComputeCommands(
	vk::CommandBuffer cb,
	const vku::Image &image,
	const pipelines::JumpFloodComputer::DescriptorSets &descriptorSets,
	std::uint32_t initialSampleOffset
) const -> bool {
	// Acquire queue family ownerships (from graphics to compute).
	if (gpu.queueFamilies.graphicsPresent != gpu.queueFamilies.compute) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
			{}, {}, {},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eShaderRead,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
				gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
				image,
				{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping */
			});
	}

	// Compute jump flood and get the last execution direction.
	auto forward = sharedData.jumpFloodComputer.compute(cb, descriptorSets, initialSampleOffset, vku::toExtent2D(image.extent));

	// Release queue family ownership.
	if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eAllCommands,
			{}, {}, {},
			vk::ImageMemoryBarrier {
				vk::AccessFlagBits::eShaderWrite, {},
				vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
				gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
				image, { vk::ImageAspectFlagBits::eColor, 0, 1, forward, 1 },
			});
	}

	return forward;
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
			passthruExtentDependentResources->primaryAttachmentGroup.colorAttachments[0].resolveImage, vku::fullSubresourceRange(),
		});

	cb.beginRenderingKHR(passthruExtentDependentResources->primaryAttachmentGroup.getRenderingInfo(
		std::array {
			vku::MsaaAttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare },
		},
		vku::MsaaAttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));

	cb.setViewport(0, passthruExtentDependentResources->primaryAttachmentGroup.getViewport(true));
	cb.setScissor(0, passthruExtentDependentResources->primaryAttachmentGroup.getScissor());

	// Render alphaMode=Opaque meshes.
	bool primitiveRendererBound = false;
	for (auto [begin, end] = sharedData.sceneResources.indirectDrawCommandBuffers.equal_range(fastgltf::AlphaMode::Opaque);
		 const auto &[criteria, indirectDrawCommandBuffer] : std::ranges::subrange(begin, end)) {
		if (!primitiveRendererBound) {
			sharedData.primitiveRenderer.bindPipeline(cb);
			sharedData.primitiveRenderer.bindDescriptorSets(cb, primitiveSets);
			sharedData.primitiveRenderer.pushConstants(cb, { task.camera.projection * task.camera.view, inverse(task.camera.view)[3] });

			primitiveRendererBound = true;
		}

		cb.setCullMode(criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

		if (const auto &indexType = criteria.indexType) {
			cb.bindIndexBuffer(sharedData.assetResources.indexBuffers.at(*indexType), 0, *indexType);
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}

	// Render alphaMode=Mask meshes.
	bool alphaMaskedPrimitiveRendererBound = false;
	for (auto [begin, end] = sharedData.sceneResources.indirectDrawCommandBuffers.equal_range(fastgltf::AlphaMode::Mask);
		 const auto &[criteria, indirectDrawCommandBuffer] : std::ranges::subrange(begin, end)) {
		if (!alphaMaskedPrimitiveRendererBound) {
			sharedData.alphaMaskedPrimitiveRenderer.bindPipeline(cb);
			// No need to have push constant, because it have same pipeline layout with PrimitiveRenderer.

			alphaMaskedPrimitiveRendererBound = true;
		}

		cb.setCullMode(criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

		if (const auto &indexType = criteria.indexType) {
			cb.bindIndexBuffer(sharedData.assetResources.indexBuffers.at(*indexType), 0, *indexType);
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}

	// TODO: render alphaMode=Blend meshes.

	// Draw skybox.
	const glm::mat4 noTranslationView = { glm::mat3 { task.camera.view } };
	if (task.useBlurredSkybox) {
		sharedData.sphericalHarmonicsRenderer.draw(cb, sphericalHarmonicsSets, {
			task.camera.projection * noTranslationView,
		});
	}
	else {
		sharedData.skyboxRenderer.draw(cb, skyboxSets, {
			task.camera.projection * noTranslationView,
		});
	}

	cb.endRenderingKHR();
}

auto vk_gltf_viewer::vulkan::Frame::recordPostCompositionCommands(
	vk::CommandBuffer cb,
	std::optional<bool> hoveringNodeJumpFloodForward,
	std::optional<bool> selectedNodeJumpFloodForward,
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
			passthruExtentDependentResources->primaryAttachmentGroup.colorAttachments[0].resolveImage, vku::fullSubresourceRange(),
		},
		// Change swapchain image layout from PresentSrcKHR to ColorAttachmentOptimal.
		vk::ImageMemoryBarrier2 {
			{}, {},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			sharedData.swapchainImages[swapchainImageIndex], vku::fullSubresourceRange(),
		},
	};
	// Acquire jumpFloodImage queue family ownership, if necessary.
	if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
		if (hoveringNodeJumpFloodForward) {
			memoryBarriers.emplace_back(
				vk::PipelineStageFlags2{}, vk::AccessFlags2{},
				vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderStorageRead,
				vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
				gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
				passthruExtentDependentResources->hoveringNodeOutlineJumpFloodResources.image,
				vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, *hoveringNodeJumpFloodForward, 1 });
		}
		if (selectedNodeJumpFloodForward) {
			memoryBarriers.emplace_back(
				vk::PipelineStageFlags2{}, vk::AccessFlags2{},
				vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderStorageRead,
				vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
				gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
				passthruExtentDependentResources->selectedNodeOutlineJumpFloodResources.image,
				vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, *selectedNodeJumpFloodForward, 1 });
		}
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
	cb.beginRenderingKHR(sharedData.swapchainAttachmentGroups[swapchainImageIndex].getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0.f, 0.f, 0.f, 0.f } }
		}));

	// Set viewport and scissor.
	cb.setViewport(0, passthruViewport);
	cb.setScissor(0, passthruScissor);

	// Draw primitive rendering image to swapchain, with Rec709 tone mapping.
	sharedData.rec709Renderer.draw(cb, rec709Sets, task.passthruRect.offset);

	cb.endRenderingKHR();

	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{},
		vk::MemoryBarrier {
			vk::AccessFlagBits::eColorAttachmentWrite,
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
		},
		{}, {});

	cb.beginRenderingKHR(sharedData.swapchainAttachmentGroups[swapchainImageIndex].getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore }
		}));

	// Draw hovering/selected node outline if exists.
	if ((task.selectedNodeIndex && task.selectedNodeOutline) || (task.hoveringNodeIndex && task.hoveringNodeOutline)) {
		sharedData.outlineRenderer.bindPipeline(cb);

		if (task.selectedNodeIndex && task.selectedNodeOutline) {
			sharedData.outlineRenderer.bindDescriptorSets(cb, selectedNodeOutlineSets);
			sharedData.outlineRenderer.pushConstants(cb, {
				.outlineColor = task.selectedNodeOutline->color,
				.passthruOffset = { task.passthruRect.offset.x, task.passthruRect.offset.y },
				.outlineThickness = task.selectedNodeOutline->thickness,
			});
			sharedData.outlineRenderer.draw(cb);
		}
		if ((task.hoveringNodeIndex && task.hoveringNodeOutline) &&
			// If both selectedNodeIndex and hoveringNodeIndex exist and are the same, the outlines will overlap, so
			// the latter one doesn’t need to be rendered.
			(!(task.selectedNodeIndex && task.selectedNodeOutline) || *task.selectedNodeIndex != *task.hoveringNodeIndex)) {
			if (task.selectedNodeIndex && task.selectedNodeOutline) {
				// TODO: pipeline barrier required but because of the reason explained in above, it is not implemented.
				//  Implement it when available.
			}

			sharedData.outlineRenderer.bindDescriptorSets(cb, hoveringNodeOutlineSets);
			sharedData.outlineRenderer.pushConstants(cb, {
				.outlineColor = task.hoveringNodeOutline->color,
				.passthruOffset = { task.passthruRect.offset.x, task.passthruRect.offset.y },
				.outlineThickness = task.hoveringNodeOutline->thickness,
			});
			sharedData.outlineRenderer.draw(cb);
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
	cb.beginRenderingKHR(sharedData.imGuiSwapchainAttachmentGroups[swapchainImageIndex].getRenderingInfo(
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
			sharedData.swapchainImages[swapchainImageIndex], vku::fullSubresourceRange(),
		});
}