module;

#include <cstdint>
#include <algorithm>
#include <array>
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

// Definition already in SharedData.cpp.
auto createCommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex) -> vk::raii::CommandPool;

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
    rec709Sets { *gpu.device, *descriptorPool, sharedData->rec709Renderer.descriptorSetLayouts },
	compositionFramebuffer { createCompositionFramebuffer(gpu.device) },
	depthPrepassFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} },
	drawFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	compositeFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
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
		    	{ sharedData->imageBasedLightingResources.value().cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize },
		    	*sharedData->imageBasedLightingResources.value().prefilteredmapImageView,
		    	*sharedData->brdfmapImageView).get(),
		    primitiveSets.getDescriptorWrites1(
				sharedData->assetResources.textures,
		    	{ sharedData->assetResources.materialBuffer.value(), 0, vk::WholeSize }).get(),
		    primitiveSets.getDescriptorWrites2(
		    	{ sharedData->sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData->sceneResources.nodeTransformBuffer, 0, vk::WholeSize }).get(),
		    skyboxSets.getDescriptorWrites0(*sharedData->imageBasedLightingResources.value().cubemapImageView).get(),
		    get<0>(outlineSets).getDescriptorWrites0(*jumpFloodImageViews[0]).get(),
		    get<1>(outlineSets).getDescriptorWrites0(*jumpFloodImageViews[1]).get(),
		    rec709Sets.getDescriptorWrites0(*primaryAttachmentGroup.colorAttachments[0].resolveView).get()),
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
	const auto jumpFloodResult = jumpFlood(gpu, jumpFloodCommandBuffer);
	draw(drawCommandBuffer);
	composite(gpu, compositeCommandBuffer, jumpFloodResult, imageIndex);

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
			rec709Sets.getDescriptorWrites0(*primaryAttachmentGroup.colorAttachments[0].resolveView).get(),
			get<0>(outlineSets).getDescriptorWrites0(*jumpFloodImageViews[0]).get(),
			get<1>(outlineSets).getDescriptorWrites0(*jumpFloodImageViews[1]).get()),
		{});

	// Recreate framebuffers.
	compositionFramebuffer = createCompositionFramebuffer(gpu.device);

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
			| vk::ImageUsageFlagBits::eStorage /* used as ping pong image in JumpFloodComputer */
			| vk::ImageUsageFlagBits::eInputAttachment /* read from OutlineRenderer */,
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
			attachmentGroup.createResolveImage(gpu.allocator, vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eInputAttachment)));
	attachmentGroup.setDepthAttachment(
		gpu.device,
		attachmentGroup.storeImage(
			attachmentGroup.createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
	return attachmentGroup;
}

auto vk_gltf_viewer::vulkan::Frame::createDescriptorPool(
    const vk::raii::Device &device
) const -> decltype(descriptorPool) {
	return {
		device,
		(vku::PoolSizes { sharedData->depthRenderer.descriptorSetLayouts }
		+ vku::PoolSizes { sharedData->jumpFloodComputer.descriptorSetLayouts } * 2
		+ vku::PoolSizes { sharedData->primitiveRenderer.descriptorSetLayouts }
		+ vku::PoolSizes { sharedData->skyboxRenderer.descriptorSetLayouts }
		+ vku::PoolSizes { sharedData->outlineRenderer.descriptorSetLayouts } * 2
		+ vku::PoolSizes { sharedData->rec709Renderer.descriptorSetLayouts })
		.getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind)
	};
}

auto vk_gltf_viewer::vulkan::Frame::createCompositionFramebuffer(
	const vk::raii::Device &device
) const -> decltype(compositionFramebuffer) {
	const std::tuple attachmentImageFormats {
		std::array { vk::Format::eR16G16B16A16Sfloat },
		std::array { vk::Format::eB8G8R8A8Srgb },
		std::array { vk::Format::eR16G16Uint },
	};
	const std::array attachmentImageInfos {
		vk::FramebufferAttachmentImageInfo {
			{},
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
			primaryAttachmentGroup.extent.width, primaryAttachmentGroup.extent.height, 1,
			get<0>(attachmentImageFormats),
		},
		vk::FramebufferAttachmentImageInfo {
			{},
			vk::ImageUsageFlagBits::eColorAttachment,
			sharedData->swapchainExtent.width, sharedData->swapchainExtent.height, 1,
			get<1>(attachmentImageFormats),
		},
		vk::FramebufferAttachmentImageInfo {
			{},
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eInputAttachment,
			jumpFloodImage.extent.width, jumpFloodImage.extent.height, 1,
			get<2>(attachmentImageFormats),
		},
		vk::FramebufferAttachmentImageInfo {
			{},
			vk::ImageUsageFlagBits::eColorAttachment,
			sharedData->swapchainExtent.width, sharedData->swapchainExtent.height, 1,
			get<1>(attachmentImageFormats),
		},
	};
	return vk::raii::Framebuffer { device, vk::StructureChain {
		vk::FramebufferCreateInfo {
			vk::FramebufferCreateFlagBits::eImageless,
			*sharedData->compositionRenderPass,
			4, nullptr,
			sharedData->swapchainExtent.width, sharedData->swapchainExtent.height,
			1,
		},
		vk::FramebufferAttachmentsCreateInfo { attachmentImageInfos },
	}.get() };
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
					{}, vk::ImageLayout::eColorAttachmentOptimal,
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
				vk::ImageLayout::eGeneral, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				jumpFloodImage,
				{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
			},
		});

	// Begin dynamic rendering.
	vku::renderingScoped(
		cb,
		depthPrepassAttachmentGroup.getRenderingInfo(
			std::array {
				vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { std::numeric_limits<std::uint32_t>::max(), 0U, 0U, 0U } },
				vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
			},
			vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 1.f, 0U } }),
		[&]() {
			// Set viewport and scissor.
			depthPrepassAttachmentGroup.setViewport(cb, true);
			depthPrepassAttachmentGroup.setScissor(cb);

			sharedData->depthRenderer.bindPipeline(cb);
			sharedData->depthRenderer.bindDescriptorSets(cb, depthSets);
			sharedData->depthRenderer.pushConstants(cb, { globalState.camera.projection * globalState.camera.view, hoveringNodeIndex.value_or(NO_INDEX) });
			for (const auto &[criteria, indirectDrawCommandBuffer] : sharedData->sceneResources.indirectDrawCommandBuffers) {
				if (const auto &indexType = criteria.indexType) {
					cb.bindIndexBuffer(sharedData->assetResources.indexBuffers.at(*indexType), 0, *indexType);
					cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
				}
				else {
					cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
				}
			}
		});

	const std::array imageMemoryBarriers {
		// For copying to hoveringNodeIndexBuffer.
		vk::ImageMemoryBarrier2 {
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
		},
		// Release jumpFloodImage[arrayLayers=0] queue family ownership.
		vk::ImageMemoryBarrier2 {
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
			{}, {},
			gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
			jumpFloodImage,
			{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
		},
	};
	cb.pipelineBarrier2KHR({
		{},
		{}, {}, imageMemoryBarriers,
	});

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
) const -> std::optional<bool> {
	cb.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	// Change image layout and acquire queue family ownership.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			{}, vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
		    gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
		    jumpFloodImage,
		    { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
		});

	std::optional isJumpFloodResultForward = [&]() -> std::optional<bool> {
		if (hoveringNodeIndex) {
			bool forward = sharedData->jumpFloodComputer.compute(cb, jumpFloodSets, vku::toExtent2D(jumpFloodImage.extent));
			// Release queue family ownership.
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eAllCommands,
				{}, {}, {},
				vk::ImageMemoryBarrier {
					vk::AccessFlagBits::eShaderWrite, {},
					{}, {},
					gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
					jumpFloodImage,
					{ vk::ImageAspectFlagBits::eColor, 0, 1, forward ? 1U : 0U, 1 },
				});
			return forward;
		}
		else return std::nullopt;
	}();

	cb.end();

	return isJumpFloodResultForward;
}

auto vk_gltf_viewer::vulkan::Frame::draw(
	vk::CommandBuffer cb
) const -> void {
	cb.begin(vk::CommandBufferBeginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	vku::renderingScoped(
		cb,
		primaryAttachmentGroup.getRenderingInfo(
			std::array {
				vku::MsaaAttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare },
			},
			vku::MsaaAttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 1.f, 0U } }),
		[&]() {
			// Set viewport and scissor.
			primaryAttachmentGroup.setViewport(cb, true);
			primaryAttachmentGroup.setScissor(cb);

			// Draw glTF mesh.
			sharedData->primitiveRenderer.bindPipeline(cb);
			sharedData->primitiveRenderer.bindDescriptorSets(cb, primitiveSets);
			sharedData->primitiveRenderer.pushConstants(cb, { globalState.camera.projection * globalState.camera.view, globalState.camera.getEye() });
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
			sharedData->skyboxRenderer.draw(cb, skyboxSets, { globalState.camera.projection * glm::mat4 { glm::mat3 { globalState.camera.view } } });
		});

	cb.end();
}

auto vk_gltf_viewer::vulkan::Frame::composite(
	const Gpu &gpu,
	vk::CommandBuffer cb,
	const std::optional<bool> &isJumpFloodResultForward,
	std::uint32_t swapchainImageIndex
) const -> void {
	cb.begin(vk::CommandBufferBeginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	// Acquire jumpFloodImage queue family ownership.
	if (isJumpFloodResultForward) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader,
			{}, {}, {},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eShaderRead,
				{}, {},
				gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
				jumpFloodImage,
				{ vk::ImageAspectFlagBits::eColor, 0, 1, *isJumpFloodResultForward, 1 },
			});
	}

	constexpr std::array<vk::ClearValue, 4> clearValues{};
	const std::array framebufferImageViews {
		*primaryAttachmentGroup.colorAttachments[0].resolveView,
		*sharedData->swapchainAttachmentGroups[swapchainImageIndex].colorAttachments[0].view,
		// If JFA not computed, use the first image (it just transition the layout to General and does not matter).
		*jumpFloodImageViews[isJumpFloodResultForward.value_or(0)],
		*sharedData->swapchainAttachmentGroups[swapchainImageIndex].colorAttachments[0].view,
	};
	vku::renderPassScoped(
		cb,
		vk::StructureChain {
			vk::RenderPassBeginInfo {
				*sharedData->compositionRenderPass,
				*compositionFramebuffer,
				{ { 0, 0 }, sharedData->swapchainExtent },
				clearValues,
			},
			vk::RenderPassAttachmentBeginInfo { framebufferImageViews },
		}.get(),
		vk::SubpassContents::eInline,
		[&]() {
			// TODO: proper viewport/scissor setting.
			sharedData->swapchainAttachmentGroups[0].setViewport(cb, true);
			sharedData->swapchainAttachmentGroups[0].setScissor(cb);

			sharedData->rec709Renderer.draw(cb, rec709Sets);

			cb.nextSubpass(vk::SubpassContents::eInline);

			if (isJumpFloodResultForward) {
				// TODO: proper viewport/scissor setting.
				sharedData->swapchainAttachmentGroups[0].setViewport(cb, true);
				sharedData->swapchainAttachmentGroups[0].setScissor(cb);

				sharedData->outlineRenderer.draw(cb, outlineSets[*isJumpFloodResultForward], { .outlineColor = { 1.f, 0.5f, 0.2f }, .lineWidth = 4.f });
			}
		});

	cb.end();
}