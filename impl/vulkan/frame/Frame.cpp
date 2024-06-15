module;

#include <cstdint>
#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <memory>
#include <print>
#include <ranges>
#include <tuple>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fastgltf/core.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.frame.Frame;

import :gltf.AssetResources;
import :gltf.SceneResources;
import :helpers.ranges;
import :io.logger;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { (Is, __VA_ARGS__)... }; })

constexpr auto NO_INDEX = std::numeric_limits<std::uint32_t>::max();

vk_gltf_viewer::vulkan::Frame::Frame(
	GlobalState &globalState,
	const std::shared_ptr<SharedData> &sharedData,
	const Gpu &gpu
) : globalState { globalState },
    sharedData { sharedData },
	jumpFloodImage { createJumpFloodImage(gpu.allocator) },
	jumpFloodImageViews { createJumpFloodImageViews(gpu.device) },
	hoveringNodeIndexBuffer {
		gpu.allocator,
		std::numeric_limits<std::uint32_t>::max(),
		vk::BufferUsageFlagBits::eTransferDst,
		vma::AllocationCreateInfo {
			vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
			vma::MemoryUsage::eAuto,
		},
	},
	depthPrepassAttachmentGroup { createDepthPrepassAttachmentGroup(gpu) },
	primaryAttachmentGroup { createPrimaryAttachmentGroup(gpu) },
    descriptorPool { createDescriptorPool(gpu.device) },
	computeCommandPool { createCommandPool(gpu.device, gpu.queueFamilies.compute) },
	graphicsCommandPool { createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent) },
	depthSets { *gpu.device, *descriptorPool, sharedData->depthRenderer.descriptorSetLayouts },
	jumpFloodSets { *gpu.device, *descriptorPool, sharedData->jumpFloodComputer.descriptorSetLayouts },
	primitiveSets { *gpu.device, *descriptorPool, sharedData->primitiveRenderer.descriptorSetLayouts },
    skyboxSets { *gpu.device, *descriptorPool, sharedData->skyboxRenderer.descriptorSetLayouts },
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
    outlineSets { ARRAY_OF(2, pipelines::OutlineRenderer::DescriptorSets { *gpu.device, *descriptorPool, sharedData->outlineRenderer.descriptorSetLayouts }) },
#pragma clang diagnostic pop
	depthPrepassFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} },
	drawFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	blitToSwapchainFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	jumpFloodFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	inFlightFence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } } {
	// Update per-frame descriptor sets.
	gpu.device.updateDescriptorSets(
	    ranges::array_cat(
	    	depthSets.getDescriptorWrites0(
		    	{ sharedData->sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData->sceneResources.nodeTransformBuffer, 0, vk::WholeSize }).get(),
	    	jumpFloodSets.getDescriptorWrites0(*jumpFloodImageViews[0], *jumpFloodImageViews[1]).get(),
		    primitiveSets.getDescriptorWrites0(
		    	{ sharedData->cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize },
		    	*sharedData->prefilteredmapImageView,
		    	*sharedData->brdfmapImageView).get(),
		    primitiveSets.getDescriptorWrites1(
				sharedData->assetResources.textures,
		    	{ sharedData->assetResources.materialBuffer.value(), 0, vk::WholeSize }).get(),
		    primitiveSets.getDescriptorWrites2(
		    	{ sharedData->sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData->sceneResources.nodeTransformBuffer, 0, vk::WholeSize }).get(),
		    skyboxSets.getDescriptorWrites0(*sharedData->cubemapImageView).get(),
		    get<0>(outlineSets).getDescriptorWrites0(*jumpFloodImageViews[0]).get(),
		    get<1>(outlineSets).getDescriptorWrites0(*jumpFloodImageViews[1]).get()),
		{});

	// Allocate per-frame command buffers.
	std::tie(jumpFloodCommandBuffer)
	    = (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	    	*computeCommandPool,
	    	vk::CommandBufferLevel::ePrimary,
	    	1,
	    })
		| ranges::to_array<1>();
	std::tie(depthPrepassCommandBuffer, drawCommandBuffer, blitToSwapchainCommandBuffer)
	    = (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	    	*graphicsCommandPool,
	    	vk::CommandBufferLevel::ePrimary,
	    	3,
	    })
		| ranges::to_array<3>();

	initAttachmentLayouts(gpu);

	io::logger::debug<true>("Frame at {} initialized", static_cast<const void*>(this));
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
	graphicsCommandPool.reset();
	computeCommandPool.reset();
	depthPrepass(gpu, depthPrepassCommandBuffer);
	jumpFlood(gpu, jumpFloodCommandBuffer);
	draw(drawCommandBuffer);
	blitToSwapchain(gpu, blitToSwapchainCommandBuffer, sharedData->swapchainAttachmentGroups[imageIndex]);

	// Submit commands to the corresponding queues.
	gpu.queues.graphicsPresent.submit(vk::SubmitInfo {
		{},
		{},
		depthPrepassCommandBuffer,
		*depthPrepassFinishSema,
	});

	constexpr std::array jumpFloodWaitStages {
		vku::toFlags(vk::PipelineStageFlagBits::eAllCommands),
	};
	gpu.queues.compute.submit(vk::SubmitInfo {
		*depthPrepassFinishSema,
		jumpFloodWaitStages,
		jumpFloodCommandBuffer,
		*jumpFloodFinishSema,
	});

	const std::array blitToSwapchainWaitSemas { *swapchainImageAcquireSema, *drawFinishSema, *jumpFloodFinishSema };
	constexpr std::array blitToSwapchainWaitStages {
		vku::toFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput),
		vku::toFlags(vk::PipelineStageFlagBits::eTransfer),
		vku::toFlags(vk::PipelineStageFlagBits::eComputeShader),
	};
	gpu.queues.graphicsPresent.submit(std::array {
		vk::SubmitInfo {
			{},
			{},
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
	jumpFloodImage = createJumpFloodImage(gpu.allocator);
	jumpFloodImageViews = createJumpFloodImageViews(gpu.device);
	depthPrepassAttachmentGroup = createDepthPrepassAttachmentGroup(gpu);
	primaryAttachmentGroup = createPrimaryAttachmentGroup(gpu);
	initAttachmentLayouts(gpu);

	// Update per-frame descriptor sets.
	gpu.device.updateDescriptorSets(
		ranges::array_cat(
			jumpFloodSets.getDescriptorWrites0(*jumpFloodImageViews[0], *jumpFloodImageViews[1]).get(),
			get<0>(outlineSets).getDescriptorWrites0(*jumpFloodImageViews[0]).get(),
			get<1>(outlineSets).getDescriptorWrites0(*jumpFloodImageViews[1]).get()),
		{});

	io::logger::debug("Swapchain resize handling for Frame finished");
}

auto vk_gltf_viewer::vulkan::Frame::createJumpFloodImage(
	vma::Allocator allocator
) const -> decltype(jumpFloodImage) {
	return { allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eR16G16Uint,
		vk::Extent3D { sharedData->swapchainExtent, 1 },
		1, 2,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eColorAttachment /* write from DepthRenderer */
			| vk::ImageUsageFlagBits::eStorage /* used as ping pong image in JumpFloodComputer */,
	}, vma::AllocationCreateInfo {
		{},
		vma::MemoryUsage::eAutoPreferDevice,
	} };
}

auto vk_gltf_viewer::vulkan::Frame::createJumpFloodImageViews(
	const vk::raii::Device &device
) const -> decltype(jumpFloodImageViews) {
	return INDEX_SEQ(Is, 2, {
		return std::array {
			vk::raii::ImageView { device, vk::ImageViewCreateInfo {
				{},
				jumpFloodImage,
				vk::ImageViewType::e2D,
				jumpFloodImage.format,
				{},
				vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, Is, 1 },
			} }...
		};
	});
}

auto vk_gltf_viewer::vulkan::Frame::createDepthPrepassAttachmentGroup(
	const Gpu &gpu
) const -> decltype(depthPrepassAttachmentGroup) {
	vku::AttachmentGroup attachmentGroup { sharedData->swapchainExtent };
	attachmentGroup.addColorAttachment(
		gpu.device,
		attachmentGroup.storeImage(
			attachmentGroup.createColorImage(gpu.allocator, vk::Format::eR32Uint, vk::ImageUsageFlagBits::eTransferSrc)));
	attachmentGroup.addColorAttachment(
		gpu.device,
		jumpFloodImage, jumpFloodImage.format, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
	attachmentGroup.setDepthAttachment(
		gpu.device,
		attachmentGroup.storeImage(
			attachmentGroup.createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
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
	attachmentGroup.setDepthAttachment(
		gpu.device,
		attachmentGroup.storeImage(
			attachmentGroup.createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
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
			vk::DescriptorType::eStorageImage,
			2 /* JumpFloodComputer pingPongImages */
			+ 2 /* OutlineRenderer jumpFloodImage */,
		},
    	vk::DescriptorPoolSize {
    	    vk::DescriptorType::eUniformBuffer,
    		1 /* PrimitiveRenderer cubemapSphericalHarmonicsBuffer */,
    	},
    };
	return { device, vk::DescriptorPoolCreateInfo{
		vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
		1 /* DepthRenderer */
		+ 1 /* JumpFloodComputer */
		+ 3 /* PrimitiveRenderer */
		+ 1 /* SkyboxRenderer */
		+ 2 /* OutlineRenderer */,
		poolSizes,
	} };
}

auto vk_gltf_viewer::vulkan::Frame::createCommandPool(
    const vk::raii::Device &device,
    std::uint32_t queueFamilyIndex
) const -> vk::raii::CommandPool {
	return { device, vk::CommandPoolCreateInfo{
		{},
		queueFamilyIndex,
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
					{}, vk::ImageLayout::eGeneral,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					jumpFloodImage, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor),
				},
				vk::ImageMemoryBarrier {
					{}, {},
					{}, vk::ImageLayout::eTransferSrcOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
				},
				vk::ImageMemoryBarrier {
					{}, {},
					{}, vk::ImageLayout::eDepthAttachmentOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					depthPrepassAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth),
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
				vk::ImageMemoryBarrier {
					{}, {},
					{}, vk::ImageLayout::eDepthAttachmentOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					primaryAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth),
				},
			});
	});
	gpu.queues.graphicsPresent.waitIdle();
}

auto vk_gltf_viewer::vulkan::Frame::update() -> void {
	if (auto value = std::exchange(hoveringNodeIndexBuffer.asValue<std::uint32_t>(), NO_INDEX); value != NO_INDEX) {
		hoveringNodeIndex = value;
	}
	else {
		hoveringNodeIndex.reset();
	}
}

auto vk_gltf_viewer::vulkan::Frame::depthPrepass(
	const Gpu &gpu,
	vk::CommandBuffer cb
) const -> void {
	cb.begin(vk::CommandBufferBeginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	// Change color attachment layout to ColorAttachmentOptimal.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{}, {}, {},
		std::array {
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
			},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eColorAttachmentWrite,
				{}, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				jumpFloodImage,
				{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
			},
		});

	// Begin dynamic rendering.
	cb.beginRenderingKHR(depthPrepassAttachmentGroup.getRenderingInfo(
		std::array {
            vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { std::numeric_limits<std::uint32_t>::max(), 0U, 0U, 0U } },
            vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
		},
		vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 1.f, 0U } }));

	// Set viewport and scissor.
	depthPrepassAttachmentGroup.setViewport(cb, true);
	depthPrepassAttachmentGroup.setScissor(cb);

	sharedData->depthRenderer.bindPipeline(cb);
	sharedData->depthRenderer.bindDescriptorSets(cb, depthSets);
	sharedData->depthRenderer.pushConstants(cb, { globalState.camera.projection * globalState.camera.view, hoveringNodeIndex.value_or(NO_INDEX) });
	for (const auto &[criteria, indirectDrawCommandBuffer] : sharedData->sceneResources.indirectDrawCommandBuffers) {
		const vk::IndexType indexType = criteria.indexType.value();
		cb.bindIndexBuffer(sharedData->assetResources.indexBuffers.at(indexType), 0, indexType);
		cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
	}

	// End dynamic rendering.
	cb.endRenderingKHR();

	{
		std::vector imageMemoryBarriers {
			// For copying to hoveringNodeIndexBuffer.
			vk::ImageMemoryBarrier2 {
				vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
				vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
			},
		};
		if (hoveringNodeIndex && gpu.queueFamilies.graphicsPresent != gpu.queueFamilies.compute) {
			// Release jumpFloodImage ownership from graphics queue family.
			imageMemoryBarriers.emplace_back(
				vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
				vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
				vk::ImageLayout::eUndefined, vk::ImageLayout::eUndefined,
				gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
				jumpFloodImage,
				vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
		}

		cb.pipelineBarrier2KHR({
			{},
			{}, {}, imageMemoryBarriers,
		});
	}

	// Copy from pixel at the cursor position to hoveringNodeIndexBuffer if cursor is inside the window.
	if (globalState.framebufferCursorPosition.x < sharedData->swapchainExtent.width &&
		globalState.framebufferCursorPosition.y < sharedData->swapchainExtent.height) {
		cb.copyImageToBuffer(
			depthPrepassAttachmentGroup.colorAttachments[0].image, vk::ImageLayout::eTransferSrcOptimal,
			hoveringNodeIndexBuffer,
			vk::BufferImageCopy {
				0, {}, {},
				{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
				{
					static_cast<std::int32_t>(globalState.framebufferCursorPosition.x),
					static_cast<std::int32_t>(globalState.framebufferCursorPosition.y),
					0,
				},
				{ 1, 1, 1 },
			});
	}

	cb.end();
}

auto vk_gltf_viewer::vulkan::Frame::jumpFlood(
	const Gpu &gpu,
	vk::CommandBuffer cb
) -> void {
	cb.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	if (hoveringNodeIndex) {
		// Change image layout and acquire queue family ownership (if required).
		const vk::ImageMemoryBarrier2 imageMemoryBarrier {
		    vk::PipelineStageFlagBits2::eAllCommands, {},
			vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead,
		    vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
		    gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
		    jumpFloodImage,
		    { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
		};
		cb.pipelineBarrier2KHR({
			{},
			{}, {}, imageMemoryBarrier,
		});

		isJumpFloodResultForward = sharedData->jumpFloodComputer.compute(cb, jumpFloodSets, vku::toExtent2D(jumpFloodImage.extent));

		// Release queue family ownership (if required).
		if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eAllCommands,
				{}, {}, {},
				vk::ImageMemoryBarrier {
					vk::AccessFlagBits::eShaderWrite, {},
					{}, {},
					gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
					jumpFloodImage,
					{ vk::ImageAspectFlagBits::eColor, 0, 1, isJumpFloodResultForward, 1 },
				});
		}
	}

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
			vku::MsaaAttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare },
		},
		vku::MsaaAttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 1.f, 0U } }));

	// Set viewport and scissor.
	primaryAttachmentGroup.setViewport(cb, true);
	primaryAttachmentGroup.setScissor(cb);

	// Draw glTF mesh.
	sharedData->primitiveRenderer.bindPipeline(cb);
	sharedData->primitiveRenderer.bindDescriptorSets(cb, primitiveSets);
	sharedData->primitiveRenderer.pushConstants(cb, { globalState.camera.projection * globalState.camera.view, globalState.camera.getEye() });
	for (const auto &[criteria, indirectDrawCommandBuffer] : sharedData->sceneResources.indirectDrawCommandBuffers) {
		const vk::IndexType indexType = criteria.indexType.value();
		cb.bindIndexBuffer(sharedData->assetResources.indexBuffers.at(indexType), 0, indexType);
		cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
	}

	// Draw skybox.
	sharedData->skyboxRenderer.draw(cb, skyboxSets, { globalState.camera.projection * glm::mat4 { glm::mat3 { globalState.camera.view } } });

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
	const Gpu &gpu,
	vk::CommandBuffer cb,
	const vku::AttachmentGroup &swapchainAttachmentGroup
) const -> void {
	cb.begin(vk::CommandBufferBeginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	// Change swapchain image layout to TransferDstOptimal.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
		{}, {}, {},
		vk::ImageMemoryBarrier{
			vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferWrite,
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

	if (hoveringNodeIndex){
		std::vector imageMemoryBarriers {
			// Change swapchain image layout to ColorAttachmentOptimal.
			vk::ImageMemoryBarrier2 {
				vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
			   vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
			   vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eColorAttachmentOptimal,
			   vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			   swapchainAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
			},
		};
		// Only do queue family ownership acquirement if the compute queue family is different from the graphics/present queue family.
		if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
			imageMemoryBarriers.emplace_back(
				vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlagBits2::eNone,
				vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead,
				vk::ImageLayout::eUndefined, vk::ImageLayout::eUndefined,
				gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
				jumpFloodImage,
				vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, isJumpFloodResultForward, 1 });
		}

		cb.pipelineBarrier2KHR({
			{},
			{}, {}, imageMemoryBarriers,
		});

		cb.beginRenderingKHR(swapchainAttachmentGroup.getRenderingInfo(
			std::array {
				vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore },
			}));

		swapchainAttachmentGroup.setViewport(cb, true);
		swapchainAttachmentGroup.setScissor(cb);

		sharedData->outlineRenderer.draw(cb, outlineSets[isJumpFloodResultForward], { .outlineColor = { 1.f, 0.5f, 0.2f }, .lineWidth = 4.f });

		cb.endRenderingKHR();

		// Change image layout to PresentSrcKHR.
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, {}, {},
			vk::ImageMemoryBarrier{
				vk::AccessFlagBits::eColorAttachmentWrite, {},
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				swapchainAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
			});
	}
	else {
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
	}

	cb.end();
}