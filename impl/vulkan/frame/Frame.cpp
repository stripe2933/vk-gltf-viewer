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
import :io.logger;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})

constexpr auto NO_INDEX = std::numeric_limits<std::uint32_t>::max();

vk_gltf_viewer::vulkan::Frame::Frame(
	const GlobalState &globalState,
	const std::shared_ptr<SharedData> &sharedData,
	const Gpu &gpu
) : globalState { globalState },
    sharedData { sharedData },
    gpu { gpu },
	hoveringNodeIndexBuffer {
		gpu.allocator,
		std::numeric_limits<std::uint32_t>::max(),
		vk::BufferUsageFlagBits::eTransferDst,
		vma::AllocationCreateInfo {
			vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
			vma::MemoryUsage::eAuto,
		},
	},
    rec709Sets { *gpu.device, *descriptorPool, sharedData->rec709Renderer.descriptorSetLayouts } {
	// Update per-frame descriptor sets.
	gpu.device.updateDescriptorSets(
	    ranges::array_cat(
	    	depthSets.getDescriptorWrites0(
		    	{ sharedData->sceneResources.primitiveBuffer, 0, vk::WholeSize },
		    	{ sharedData->sceneResources.nodeTransformBuffer, 0, vk::WholeSize }).get(),
	    	jumpFloodSets.getDescriptorWrites0(*jumpFloodImageViews.ping, *jumpFloodImageViews.pong).get(),
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

	initAttachmentLayouts();

	io::logger::debug<true>("Frame at {} initialized", static_cast<const void*>(this));
}

auto vk_gltf_viewer::vulkan::Frame::onLoop() -> std::expected<OnLoopResult, OnLoopError> {
	constexpr std::uint64_t MAX_TIMEOUT = std::numeric_limits<std::uint64_t>::max();

	// Wait for the previous frame execution to finish.
	if (auto result = gpu.device.waitForFences(*inFlightFence, true, MAX_TIMEOUT); result != vk::Result::eSuccess) {
		throw std::runtime_error{ std::format("Failed to wait for in-flight fence: {}", to_string(result)) };
	}
	gpu.device.resetFences(*inFlightFence);

	OnLoopResult result{};

	// Update per-frame resources.
	update(result);

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
	recordDepthPrepassCommands(depthPrepassCommandBuffer);
	depthPrepassCommandBuffer.end();

	jumpFloodCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	const std::optional<bool> jumpFloodResultForward = recordJumpFloodCalculationCommands(jumpFloodCommandBuffer);
	jumpFloodCommandBuffer.end();
	if (jumpFloodResultForward.has_value()) {
		gpu.device.updateDescriptorSets(
			outlineSets.getDescriptorWrites0(*jumpFloodResultForward ? *jumpFloodImageViews.pong : *jumpFloodImageViews.ping).get(),
			{});
	}

	drawCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	recordGltfPrimitiveDrawCommands(drawCommandBuffer);
	drawCommandBuffer.end();

	compositeCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	recordPostCompositionCommands(compositeCommandBuffer, jumpFloodResultForward, imageIndex);
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
	jumpFloodImage = createJumpFloodImage();
	jumpFloodImageViews = createJumpFloodImageViews();
	depthPrepassAttachmentGroup = createDepthPrepassAttachmentGroup();
	primaryAttachmentGroup = createPrimaryAttachmentGroup();
	initAttachmentLayouts();

	// Update per-frame descriptor sets.
	gpu.device.updateDescriptorSets(
		ranges::array_cat(
		    jumpFloodSets.getDescriptorWrites0(*jumpFloodImageViews.ping, *jumpFloodImageViews.pong).get(),
			rec709Sets.getDescriptorWrites0(*primaryAttachmentGroup.colorAttachments[0].resolveView).get()),
		{});

	// Recreate framebuffers.
	compositionFramebuffer = createCompositionFramebuffer();

	io::logger::debug("Swapchain resize handling for Frame finished");
}

auto vk_gltf_viewer::vulkan::Frame::createJumpFloodImage() const -> decltype(jumpFloodImage) {
	return { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eR16G16B16A16Uint,
		vk::Extent3D { sharedData->swapchainExtent, 1 },
		1, 2, // arrayLevels=0 for ping image, arrayLevels=1 for pong image.
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

auto vk_gltf_viewer::vulkan::Frame::createJumpFloodImageViews() const -> decltype(jumpFloodImageViews) {
	return INDEX_SEQ(Is, 2, {
		return decltype(jumpFloodImageViews) {
			vk::raii::ImageView { gpu.device, vk::ImageViewCreateInfo {
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

auto vk_gltf_viewer::vulkan::Frame::createDepthPrepassAttachmentGroup() const -> decltype(depthPrepassAttachmentGroup) {
	vku::AttachmentGroup attachmentGroup { sharedData->swapchainExtent };
	attachmentGroup.addColorAttachment(
		gpu.device,
		attachmentGroup.storeImage(
			attachmentGroup.createColorImage(gpu.allocator, vk::Format::eR32Uint, vk::ImageUsageFlagBits::eTransferSrc)));
	attachmentGroup.addColorAttachment(
		gpu.device,
		jumpFloodImage, jumpFloodImage.format,
		{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image subresource */);
	attachmentGroup.setDepthAttachment(
		gpu.device,
		attachmentGroup.storeImage(
			attachmentGroup.createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
	return attachmentGroup;
}

auto vk_gltf_viewer::vulkan::Frame::createPrimaryAttachmentGroup() const -> decltype(primaryAttachmentGroup) {
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

auto vk_gltf_viewer::vulkan::Frame::createCompositionFramebuffer() const -> decltype(compositionFramebuffer) {
	const std::tuple attachmentImageFormats {
		std::array { vk::Format::eR16G16B16A16Sfloat }, // Primitive rendering image.
		std::array { vk::Format::eB8G8R8A8Srgb, vk::Format::eB8G8R8A8Unorm }, // Swapchain image for ImGui.
		std::array { vk::Format::eR16G16B16A16Uint }, // Jump flood calculation image.
	};
	const std::array attachmentImageInfos {
		vk::FramebufferAttachmentImageInfo {
			{},
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
			primaryAttachmentGroup.extent.width, primaryAttachmentGroup.extent.height, 1,
			get<0>(attachmentImageFormats),
		},
		vk::FramebufferAttachmentImageInfo {
			vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage,
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
			vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage,
			vk::ImageUsageFlagBits::eColorAttachment,
			sharedData->swapchainExtent.width, sharedData->swapchainExtent.height, 1,
			get<1>(attachmentImageFormats),
		},
		vk::FramebufferAttachmentImageInfo {
			vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage,
			vk::ImageUsageFlagBits::eColorAttachment,
			sharedData->swapchainExtent.width, sharedData->swapchainExtent.height, 1,
			get<1>(attachmentImageFormats),
		},
	};
	return vk::raii::Framebuffer { gpu.device, vk::StructureChain {
		vk::FramebufferCreateInfo {
			vk::FramebufferCreateFlagBits::eImageless,
			*sharedData->compositionRenderPass,
			attachmentImageInfos.size(), nullptr,
			sharedData->swapchainExtent.width, sharedData->swapchainExtent.height,
			1,
		},
		vk::FramebufferAttachmentsCreateInfo { attachmentImageInfos },
	}.get() };
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

	appendBarrier(vk::ImageLayout::eGeneral, jumpFloodImage);
	appendBarrier(vk::ImageLayout::eTransferSrcOptimal, depthPrepassAttachmentGroup.colorAttachments[0].image);
	appendBarrier(vk::ImageLayout::eDepthAttachmentOptimal, depthPrepassAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth));
	appendBarrier(vk::ImageLayout::eColorAttachmentOptimal, primaryAttachmentGroup.colorAttachments[0].image);
	appendBarrier(vk::ImageLayout::eColorAttachmentOptimal, primaryAttachmentGroup.colorAttachments[0].resolveImage);
	appendBarrier(vk::ImageLayout::eDepthAttachmentOptimal, primaryAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth));

	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, {}, {}, barriers);
	});
	gpu.queues.graphicsPresent.waitIdle();
}

auto vk_gltf_viewer::vulkan::Frame::update(
	OnLoopResult &result
) -> void {
	// Get node index under the cursor from hoveringNodeIndexBuffer.
	// If it is not NO_INDEX (i.e. node index is found), update hoveringNodeIndex.
	if (auto value = std::exchange(hoveringNodeIndexBuffer.asValue<std::uint32_t>(), NO_INDEX); value != NO_INDEX) {
		result.hoveringNodeIndex = value;
	}
}

auto vk_gltf_viewer::vulkan::Frame::recordDepthPrepassCommands(
	vk::CommandBuffer cb
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
				depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
			},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageLayout::eGeneral, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				jumpFloodImage,
				{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
			},
		});

	cb.beginRenderingKHR(depthPrepassAttachmentGroup.getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { std::numeric_limits<std::uint32_t>::max(), 0U, 0U, 0U } },
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
		},
		vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 1.f, 0U } }));
	[&]() {
		// Set viewport and scissor.
		depthPrepassAttachmentGroup.setViewport(cb, true);
		depthPrepassAttachmentGroup.setScissor(cb);

		sharedData->depthRenderer.bindPipeline(cb);
		sharedData->depthRenderer.bindDescriptorSets(cb, depthSets);
		sharedData->depthRenderer.pushConstants(cb, {
			globalState.camera.projection * globalState.camera.view,
			globalState.hoveringNodeIndex.value_or(NO_INDEX),
			globalState.selectedNodeIndex.value_or(NO_INDEX),
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
	}();
	cb.endRenderingKHR();

	const std::array imageMemoryBarriers {
		// For copying to hoveringNodeIndexBuffer.
		vk::ImageMemoryBarrier2 {
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
		},
		// Release jump flood resources' ping images queue family ownership.
		vk::ImageMemoryBarrier2 {
			vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
			{}, {},
			gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
			jumpFloodImage,
			{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
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
}

auto vk_gltf_viewer::vulkan::Frame::recordJumpFloodCalculationCommands(
	vk::CommandBuffer cb
) const -> std::optional<bool> {
	// Change image layout and acquire queue family ownership.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			{}, vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
		    gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
		    jumpFloodImage,
		    { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping */,
		});

	if (globalState.hoveringNodeIndex || globalState.selectedNodeIndex) {
		auto forward = sharedData->jumpFloodComputer.compute(cb, jumpFloodSets, vku::toExtent2D(jumpFloodImage.extent));
		// Release queue family ownership.
		if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eAllCommands,
				{}, {}, {},
				vk::ImageMemoryBarrier {
					vk::AccessFlagBits::eShaderWrite, {},
					{}, {},
					gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
					jumpFloodImage, { vk::ImageAspectFlagBits::eColor, 0, 1, forward, 1 },
				});
		}
		return forward;
	}
	return std::nullopt;
}

auto vk_gltf_viewer::vulkan::Frame::recordGltfPrimitiveDrawCommands(
	vk::CommandBuffer cb
) const -> void {
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
	const glm::mat4 noTranslationView = { glm::mat3 { globalState.camera.view } };
	sharedData->skyboxRenderer.draw(cb, skyboxSets, {
		globalState.camera.projection * noTranslationView,
	});

	cb.endRenderingKHR();
}

auto vk_gltf_viewer::vulkan::Frame::recordPostCompositionCommands(
	vk::CommandBuffer cb,
	std::optional<bool> isJumpFloodResultForward,
	std::uint32_t swapchainImageIndex
) const -> void {
	// Acquire jumpFloodImage queue family ownership.
	if ((globalState.hoveringNodeIndex || globalState.selectedNodeIndex) && gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader,
			{}, {}, {},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eShaderRead,
				vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
				gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
				jumpFloodImage, { vk::ImageAspectFlagBits::eColor, 0, 1, *isJumpFloodResultForward, 1 },
			});
	}

	constexpr std::array<vk::ClearValue, 5> clearValues{};
	const std::array framebufferImageViews {
		*primaryAttachmentGroup.colorAttachments[0].resolveView,
		*sharedData->swapchainAttachmentGroups[swapchainImageIndex].colorAttachments[0].view,
		// If JFA not computed, use the first image (it just transition the layout to General and does not matter).
		(isJumpFloodResultForward && *isJumpFloodResultForward) ? *jumpFloodImageViews.pong : *jumpFloodImageViews.ping,
		*sharedData->swapchainAttachmentGroups[swapchainImageIndex].colorAttachments[0].view,
		*sharedData->imGuiSwapchainAttachmentGroups[swapchainImageIndex].colorAttachments[0].view,
	};
	static_assert(clearValues.size() == framebufferImageViews.size(), "Clear count mismatch");

	// Start render pass.
	cb.beginRenderPass(vk::StructureChain {
		vk::RenderPassBeginInfo {
			*sharedData->compositionRenderPass,
			*compositionFramebuffer,
			{ { 0, 0 }, sharedData->swapchainExtent },
			clearValues,
		},
		vk::RenderPassAttachmentBeginInfo { framebufferImageViews },
	}.get(), vk::SubpassContents::eInline);

	sharedData->swapchainAttachmentGroups[swapchainImageIndex].setViewport(cb, true);
	sharedData->swapchainAttachmentGroups[swapchainImageIndex].setScissor(cb);

	// Draw primitive rendering image to swapchain, with Rec709 tone mapping.
	sharedData->rec709Renderer.draw(cb, rec709Sets);

	cb.nextSubpass(vk::SubpassContents::eInline);

	// Draw hovering/selected node outline if exists.
	[&]() {
		if (globalState.hoveringNodeIndex || globalState.selectedNodeIndex) {
			sharedData->outlineRenderer.bindPipeline(cb);
			sharedData->outlineRenderer.bindDescriptorSets(cb, outlineSets);

			if (globalState.selectedNodeIndex) {
				sharedData->outlineRenderer.pushConstants(cb, {
					.outlineColor = { 0.f, 1.f, 0.2f },
					.outlineThickness = 4.f,
					.useZwComponent = true,
				});
				sharedData->outlineRenderer.draw(cb);
			}
			if (globalState.selectedNodeIndex && globalState.hoveringNodeIndex) {
				// Special case: if both selectedNodeIndex and hoveringNodeIndex exist and are the same, the
				// outlines will overlap, so the latter one doesn’t need to be rendered.
				if (*globalState.selectedNodeIndex == *globalState.hoveringNodeIndex) {
					return;
				}

				// If we need to draw the outlines of both the hovering node and the selected node, a memory barrier
				// is required between the two overlapping drawings.
				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
					vk::DependencyFlagBits::eByRegion,
					vk::MemoryBarrier {
						vk::AccessFlagBits::eColorAttachmentWrite,
						vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
					}, {}, {});
			}
			if (globalState.hoveringNodeIndex) {
				sharedData->outlineRenderer.pushConstants(cb, {
					.outlineColor = { 1.f, 0.5f, 0.2f },
					.outlineThickness = 4.f,
					.useZwComponent = false,
				});
				sharedData->outlineRenderer.draw(cb);
			}
		}
	}();

	cb.nextSubpass(vk::SubpassContents::eInline);

	// Draw ImGui.
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

    cb.endRenderPass();
}