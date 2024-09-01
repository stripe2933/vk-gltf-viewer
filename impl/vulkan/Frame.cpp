module;

#include <fastgltf/core.hpp>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.Frame;

import std;
import ranges;
import type_variant;
import :helpers.functional;
import :vulkan.ag.DepthPrepass;
import :vulkan.ag.Scene;

constexpr auto NO_INDEX = std::numeric_limits<std::uint32_t>::max();

vk_gltf_viewer::vulkan::Frame::Frame(const Gpu &gpu, const SharedData &sharedData)
	: gpu { gpu }
	, hoveringNodeIndexBuffer { gpu.allocator, NO_INDEX, vk::BufferUsageFlagBits::eTransferDst, vku::allocation::hostRead }
	, sharedData { sharedData } {
	// Change initial attachment layouts.
	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, {}, {},
			{
				vk::ImageMemoryBarrier {
					{}, {},
					{}, vk::ImageLayout::eColorAttachmentOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					sceneAttachmentGroup.getSwapchainAttachment(0).image, vku::fullSubresourceRange(),
				},
				vk::ImageMemoryBarrier {
					{}, {},
					{}, vk::ImageLayout::eDepthAttachmentOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					sceneAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth),
				},
			});
	});

	// Allocate descriptor sets.
	std::tie(hoveringNodeJumpFloodSet, selectedNodeJumpFloodSet, hoveringNodeOutlineSet, selectedNodeOutlineSet)
		= allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(
			sharedData.jumpFloodComputer.descriptorSetLayout,
			sharedData.jumpFloodComputer.descriptorSetLayout,
			sharedData.outlineRenderer.descriptorSetLayout,
			sharedData.outlineRenderer.descriptorSetLayout));

	// Allocate per-frame command buffers.
	std::tie(jumpFloodCommandBuffer)
	    = (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	    	*computeCommandPool,
	    	vk::CommandBufferLevel::ePrimary,
	    	1,
	    })
		| ranges::to_array<1>();
	std::tie(scenePrepassCommandBuffer, sceneRenderingCommandBuffer, compositionCommandBuffer)
	    = (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	    	*graphicsCommandPool,
	    	vk::CommandBufferLevel::ePrimary,
	    	3,
	    })
		| ranges::to_array<3>();
}

auto vk_gltf_viewer::vulkan::Frame::update(const ExecutionTask &task) -> UpdateResult {
	UpdateResult result{};

	if (task.handleSwapchainResize) {
		// Attachment images that have to be matched to the swapchain extent must be recreated.
		sceneAttachmentGroup = { gpu, sharedData.swapchainExtent, sharedData.swapchainImages };
	}

	// Get node index under the cursor from hoveringNodeIndexBuffer.
	// If it is not NO_INDEX (i.e. node index is found), update hoveringNodeIndex.
	if (auto value = std::exchange(hoveringNodeIndexBuffer.asValue<std::uint32_t>(), NO_INDEX); value != NO_INDEX) {
		result.hoveringNodeIndex = value;
	}

	// If passthru extent is different from the current's, dependent images have to be recreated.
	if (!passthruExtent || *passthruExtent != task.passthruRect.extent) {
		passthruExtent.emplace(task.passthruRect.extent);
		// TODO: can this operation be non-blocking?
		const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
		vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
			passthruResources.emplace(gpu, *passthruExtent, cb);
		}, *fence);
		if (gpu.device.waitForFences(*fence, true, ~0ULL) != vk::Result::eSuccess) {
			throw std::runtime_error { "Failed to initialize the rendering region GPU resources." };
		}

		gpu.device.updateDescriptorSets({
			hoveringNodeJumpFloodSet.getWriteOne<0>({ {}, *passthruResources->hoveringNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
			selectedNodeJumpFloodSet.getWriteOne<0>({ {}, *passthruResources->selectedNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
		}, {});
	}

	projectionViewMatrix = task.camera.projection * task.camera.view;
	viewPosition = inverse(task.camera.view)[3];
	translationlessProjectionViewMatrix = task.camera.projection * glm::mat4 { glm::mat3 { task.camera.view } };
	passthruRect = task.passthruRect;
	cursorPosFromPassthruRectTopLeft
		= task.mouseCursorOffset.and_then([&](vk::Offset2D offset) -> std::optional<vk::Offset2D> {
			offset.x -= passthruRect.offset.x;
			offset.y -= passthruRect.offset.y;

			if (0 <= offset.x && offset.x < passthruRect.extent.width && 0 <= offset.y && offset.y < passthruRect.extent.height) {
			    return offset;
			}
			else {
				return std::nullopt;
			}
		});

	// If there is a glTF scene to be rendered, related resources have to be updated.
	if (task.gltf) {
		indexBuffers = task.gltf->indexBuffers
			| ranges::views::value_transform([](vk::Buffer buffer) { return buffer; })
			| std::ranges::to<std::unordered_map>();

		const auto criteriaGetter = [&](const gltf::AssetResources::PrimitiveInfo &primitiveInfo) {
			CommandSeparationCriteria result {
				.alphaMode = fastgltf::AlphaMode::Opaque,
				.faceted = primitiveInfo.normalInfo.has_value(),
				.doubleSided = false,
				.indexType = primitiveInfo.indexInfo.transform([](const auto &info) { return info.type; }),
			};
			if (primitiveInfo.materialIndex) {
				const fastgltf::Material &material = task.gltf->asset.materials[*primitiveInfo.materialIndex];
				result.alphaMode = material.alphaMode;
				result.doubleSided = material.doubleSided;
			}
			return result;
		};

		if (!task.gltf->renderingNodeIndices.empty()) {
			if (!renderingNodes || (renderingNodes && renderingNodes->indices != task.gltf->renderingNodeIndices)) {
				renderingNodes.emplace(
					task.gltf->renderingNodeIndices,
					task.gltf->sceneResources.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, task.gltf->renderingNodeIndices));
			}
		}
		else {
			renderingNodes.reset();
		}

		if (!task.gltf->selectedNodeIndices.empty() && task.selectedNodeOutline) {
			if (selectedNodes) {
				if (selectedNodes->indices != task.gltf->selectedNodeIndices) {
					selectedNodes->indices = task.gltf->selectedNodeIndices;
					selectedNodes->indirectDrawCommandBuffers = task.gltf->sceneResources.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, task.gltf->selectedNodeIndices);
				}
				selectedNodes->outlineColor = task.selectedNodeOutline->color;
				selectedNodes->outlineThickness = task.selectedNodeOutline->thickness;
			}
			else {
				selectedNodes.emplace(
					task.gltf->selectedNodeIndices,
					task.gltf->sceneResources.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, task.gltf->selectedNodeIndices),
					task.selectedNodeOutline->color,
					task.selectedNodeOutline->thickness);
			}
		}
		else {
			selectedNodes.reset();
		}

		if (task.gltf->hoveringNodeIndex && task.hoveringNodeOutline &&
			// If selectedNodeIndices == hoveringNodeIndex, hovering node outline doesn't have to be drawn.
			!(task.gltf->selectedNodeIndices.size() == 1 && *task.gltf->selectedNodeIndices.begin() == *task.gltf->hoveringNodeIndex)) {
			if (hoveringNode) {
				if (hoveringNode->index != *task.gltf->hoveringNodeIndex) {
					hoveringNode->index = *task.gltf->hoveringNodeIndex;
					hoveringNode->indirectDrawCommandBuffers = task.gltf->sceneResources.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, { *task.gltf->hoveringNodeIndex });
				}
				hoveringNode->outlineColor = task.hoveringNodeOutline->color;
				hoveringNode->outlineThickness = task.hoveringNodeOutline->thickness;
			}
			else {
				hoveringNode.emplace(
					*task.gltf->hoveringNodeIndex,
					task.gltf->sceneResources.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, { *task.gltf->hoveringNodeIndex }),
					task.hoveringNodeOutline->color,
					task.hoveringNodeOutline->thickness);
			}
		}
		else {
			hoveringNode.reset();
		}
	}

	if (task.solidBackground) {
		background.emplace<glm::vec3>(*task.solidBackground);
	}
	else {
		background.emplace<vku::DescriptorSet<dsl::Skybox>>(sharedData.skyboxDescriptorSet);
	}

	return result;
}

auto vk_gltf_viewer::vulkan::Frame::execute() const -> bool {
	// Acquire the next swapchain image.
	std::uint32_t imageIndex;
	try {
		imageIndex = (*gpu.device).acquireNextImageKHR(*sharedData.swapchain, ~0ULL, *swapchainImageAcquireSema).value;
	}
	catch (const vk::OutOfDateKHRError&) {
		return false;
	}

	// Record commands.
	graphicsCommandPool.reset();
	computeCommandPool.reset();

	// Depth prepass and jump flood seed image calculation pass.
	{
		scenePrepassCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		recordScenePrepassCommands(scenePrepassCommandBuffer);
		scenePrepassCommandBuffer.end();
	}

	// Jump flood calculation pass.
	// TODO: If there are multiple compute queues, distribute the tasks to avoid the compute pipeline stalling.
	std::optional<bool> hoveringNodeJumpFloodForward{}, selectedNodeJumpFloodForward{};
	{
		jumpFloodCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		if (hoveringNode) {
			hoveringNodeJumpFloodForward = recordJumpFloodComputeCommands(
				jumpFloodCommandBuffer,
				passthruResources->hoveringNodeOutlineJumpFloodResources.image,
				hoveringNodeJumpFloodSet,
				std::bit_ceil(static_cast<std::uint32_t>(hoveringNode->outlineThickness)));
			gpu.device.updateDescriptorSets(
				hoveringNodeOutlineSet.getWriteOne<0>({
					{},
					*hoveringNodeJumpFloodForward
						? *passthruResources->hoveringNodeOutlineJumpFloodResources.pongImageView
						: *passthruResources->hoveringNodeOutlineJumpFloodResources.pingImageView,
					vk::ImageLayout::eShaderReadOnlyOptimal,
				}),
				{});
		}
		if (selectedNodes) {
			selectedNodeJumpFloodForward = recordJumpFloodComputeCommands(
				jumpFloodCommandBuffer,
				passthruResources->selectedNodeOutlineJumpFloodResources.image,
				selectedNodeJumpFloodSet,
				std::bit_ceil(static_cast<std::uint32_t>(selectedNodes->outlineThickness)));
			gpu.device.updateDescriptorSets(
				selectedNodeOutlineSet.getWriteOne<0>({
					{},
					*selectedNodeJumpFloodForward
						? *passthruResources->selectedNodeOutlineJumpFloodResources.pongImageView
						: *passthruResources->selectedNodeOutlineJumpFloodResources.pingImageView,
					vk::ImageLayout::eShaderReadOnlyOptimal,
				}),
				{});
		}
		jumpFloodCommandBuffer.end();
	}

	// glTF scene rendering pass.
	{
		sceneRenderingCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		// Change swapchain image layout from PresentSrcKHR to ColorAttachmentOptimal.
		sceneRenderingCommandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
			{}, {}, {},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eColorAttachmentOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				sharedData.swapchainImages[imageIndex], vku::fullSubresourceRange(),
			});

		vk::ClearColorValue backgroundColor { 0.f, 0.f, 0.f, 0.f };
		if (auto *clearColor = get_if<glm::vec3>(&background)) {
			backgroundColor.setFloat32({ clearColor->x, clearColor->y, clearColor->z, 1.f });
		}
		sceneRenderingCommandBuffer.beginRenderingKHR(sceneAttachmentGroup.getRenderingInfo(
			vku::MsaaAttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, backgroundColor },
			vku::MsaaAttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } },
			imageIndex));

		const vk::Viewport passthruViewport {
			// Use negative viewport.
			static_cast<float>(passthruRect.offset.x), static_cast<float>(passthruRect.offset.y + passthruRect.extent.height),
			static_cast<float>(passthruRect.extent.width), -static_cast<float>(passthruRect.extent.height),
			0.f, 1.f,
		};
		sceneRenderingCommandBuffer.setViewport(0, passthruViewport);
		sceneRenderingCommandBuffer.setScissor(0, passthruRect);

		if (renderingNodes) {
			recordSceneDrawCommands(sceneRenderingCommandBuffer);
		}
		if (holds_alternative<vku::DescriptorSet<dsl::Skybox>>(background)) {
			recordSkyboxDrawCommands(sceneRenderingCommandBuffer);
		}

		sceneRenderingCommandBuffer.endRenderingKHR();

		sceneRenderingCommandBuffer.end();
	}

	// Post-composition pass.
	{
		compositionCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		if (selectedNodes || hoveringNode) {
			recordNodeOutlineCompositionCommands(compositionCommandBuffer, hoveringNodeJumpFloodForward, selectedNodeJumpFloodForward, imageIndex);

			// Make sure the outline composition is done before rendering ImGui.
			compositionCommandBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
				{},
				vk::MemoryBarrier {
					vk::AccessFlagBits::eColorAttachmentWrite,
					vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
				},
				{}, {});
		}

		recordImGuiCompositionCommands(compositionCommandBuffer, imageIndex);

		// Change swapchain image layout from ColorAttachmentOptimal to PresentSrcKHR.
		compositionCommandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, {}, {},
			vk::ImageMemoryBarrier {
				vk::AccessFlagBits::eColorAttachmentWrite, {},
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				sharedData.swapchainImages[imageIndex], vku::fullSubresourceRange(),
			});

		compositionCommandBuffer.end();
	}

	// Submit commands to the corresponding queues.
	gpu.queues.graphicsPresent.submit(vk::SubmitInfo {
		{},
		{},
		scenePrepassCommandBuffer,
		*scenePrepassFinishSema,
	});

	gpu.queues.compute.submit(vk::SubmitInfo {
		*scenePrepassFinishSema,
		vku::unsafeProxy({
			vk::Flags { vk::PipelineStageFlagBits::eComputeShader },
		}),
		jumpFloodCommandBuffer,
		*jumpFloodFinishSema,
	});

	gpu.queues.graphicsPresent.submit({
		vk::SubmitInfo {
			*swapchainImageAcquireSema,
			vku::unsafeProxy(vk::Flags { vk::PipelineStageFlagBits::eColorAttachmentOutput }),
			sceneRenderingCommandBuffer,
			*sceneRenderingFinishSema,
		},
		vk::SubmitInfo {
			vku::unsafeProxy({ *sceneRenderingFinishSema, *jumpFloodFinishSema }),
			vku::unsafeProxy({
				vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
				vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
			}),
			compositionCommandBuffer,
			*compositionFinishSema,
		},
	}, *inFlightFence);

	// Present the image to the swapchain.
	try {
		// The result codes VK_ERROR_OUT_OF_DATE_KHR and VK_SUBOPTIMAL_KHR have the same meaning when
		// returned by vkQueuePresentKHR as they do when returned by vkAcquireNextImageKHR.
		if (gpu.queues.graphicsPresent.presentKHR({ *compositionFinishSema, *sharedData.swapchain, imageIndex }) == vk::Result::eSuboptimalKHR) {
			return false;
		}
	}
	catch (const vk::OutOfDateKHRError&) {
		return false;
	}

	return true;
}

vk_gltf_viewer::vulkan::Frame::PassthruResources::JumpFloodResources::JumpFloodResources(
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
		vk::ImageUsageFlagBits::eColorAttachment /* write from DepthRenderer */
			| vk::ImageUsageFlagBits::eStorage /* used as ping pong image in JumpFloodComputer */
			| vk::ImageUsageFlagBits::eSampled /* read in OutlineRenderer */,
		gpu.queueFamilies.getUniqueIndices().size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
		vku::unsafeProxy(gpu.queueFamilies.getUniqueIndices()),
	} },
	imageView { gpu.device, image.getViewCreateInfo(vk::ImageViewType::e2DArray) },
	pingImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) },
	pongImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 }) } { }

vk_gltf_viewer::vulkan::Frame::PassthruResources::PassthruResources(
	const Gpu &gpu,
	const vk::Extent2D &extent,
	vk::CommandBuffer graphicsCommandBuffer
) : hoveringNodeOutlineJumpFloodResources { gpu, extent },
	selectedNodeOutlineJumpFloodResources { gpu, extent },
	depthPrepassAttachmentGroup { gpu, extent },
	hoveringNodeJumpFloodSeedAttachmentGroup { gpu, hoveringNodeOutlineJumpFloodResources.image },
	selectedNodeJumpFloodSeedAttachmentGroup { gpu, selectedNodeOutlineJumpFloodResources.image } {
	recordInitialImageLayoutTransitionCommands(graphicsCommandBuffer);
}

auto vk_gltf_viewer::vulkan::Frame::PassthruResources::recordInitialImageLayoutTransitionCommands(
    vk::CommandBuffer graphicsCommandBuffer
) const -> void {
	constexpr auto layoutTransitionBarrier = [](
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
			layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, depthPrepassAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
			layoutTransitionBarrier(vk::ImageLayout::eGeneral, hoveringNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 } /* pong image */),
			layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, hoveringNodeJumpFloodSeedAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
			layoutTransitionBarrier(vk::ImageLayout::eGeneral, selectedNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 } /* pong image */),
			layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, selectedNodeJumpFloodSeedAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
		});
}

auto vk_gltf_viewer::vulkan::Frame::createDescriptorPool() const -> decltype(descriptorPool) {
	return {
		gpu.device,
		(2 * getPoolSizes(sharedData.jumpFloodComputer.descriptorSetLayout, sharedData.outlineRenderer.descriptorSetLayout))
			.getDescriptorPoolCreateInfo(),
	};
}

auto vk_gltf_viewer::vulkan::Frame::recordScenePrepassCommands(vk::CommandBuffer cb) const -> void {
	std::vector<vk::ImageMemoryBarrier> memoryBarriers{}; // TODO: use static_vector with capacity=3.

	// If glTF Scene have to be rendered, prepare attachment layout transition for node index and depth rendering.
	if (renderingNodes) {
		memoryBarriers.push_back({
			{}, vk::AccessFlagBits::eColorAttachmentWrite,
			{}, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			passthruResources->depthPrepassAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
		});
	}

	// If hovering node's outline have to be rendered, prepare attachment layout transition for jump flood seeding.
	const auto addJumpFloodSeedImageMemoryBarrier = [&](vk::Image image) {
		memoryBarriers.push_back({
			{}, vk::AccessFlagBits::eColorAttachmentWrite,
			{}, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
		});
	};
	if (selectedNodes) {
		addJumpFloodSeedImageMemoryBarrier(passthruResources->selectedNodeOutlineJumpFloodResources.image);
	}
	// Same holds for hovering nodes' outline.
	if (hoveringNode) {
		addJumpFloodSeedImageMemoryBarrier(passthruResources->hoveringNodeOutlineJumpFloodResources.image);
	}

	// Attachment layout transitions.
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
		{}, {}, {}, memoryBarriers);

	cb.setViewport(0, vku::toViewport(*passthruExtent, true));
	cb.setScissor(0, vk::Rect2D { { 0, 0 }, *passthruExtent });

	ResourceBindingState resourceBindingState{};
	const auto drawPrimitives
		= [&](
			const std::map<CommandSeparationCriteria, vku::MappedBuffer, CommandSeparationCriteriaComparator> &indirectDrawCommandBuffers,
			const auto &opaqueOrBlendRenderer,
			const auto &maskedRenderer
		) {
			// Render alphaMode=Opaque or BLEND meshes.
			const auto drawOpaqueOrBlendMesh = [&](fastgltf::AlphaMode alphaMode) {
    			assert(alphaMode == fastgltf::AlphaMode::Opaque || alphaMode == fastgltf::AlphaMode::Blend);
				for (auto [begin, end] = indirectDrawCommandBuffers.equal_range(alphaMode);
					 const auto &[criteria, indirectDrawCommandBuffer] : std::ranges::subrange(begin, end)) {

					if (!resourceBindingState.boundPipeline.holds_alternative<std::remove_cvref_t<decltype(opaqueOrBlendRenderer)>>()) {
						opaqueOrBlendRenderer.bindPipeline(cb);
						resourceBindingState.boundPipeline.emplace<std::remove_cvref_t<decltype(opaqueOrBlendRenderer)>>();
					}

					if (!resourceBindingState.sceneDescriptorSetBound) {
						cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *opaqueOrBlendRenderer.pipelineLayout, 0, sharedData.sceneDescriptorSet, {});
						resourceBindingState.sceneDescriptorSetBound = true;
					}

					if (!resourceBindingState.pushConstantBound) {
						opaqueOrBlendRenderer.pushConstants(cb, { projectionViewMatrix });
						resourceBindingState.pushConstantBound = true;
					}

					if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; resourceBindingState.cullMode != cullMode) {
						cb.setCullMode(resourceBindingState.cullMode.emplace(cullMode));
					}

					if (const auto &indexType = criteria.indexType) {
						if (resourceBindingState.indexBuffer != *indexType) {
							resourceBindingState.indexBuffer.emplace(*indexType);
							cb.bindIndexBuffer(indexBuffers.at(*indexType), 0, *indexType);
						}
						cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
					}
					else {
						cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
					}
				}
			};
			drawOpaqueOrBlendMesh(fastgltf::AlphaMode::Opaque);
			drawOpaqueOrBlendMesh(fastgltf::AlphaMode::Blend);

			// Render alphaMode=Mask meshes.
			for (auto [begin, end] = indirectDrawCommandBuffers.equal_range(fastgltf::AlphaMode::Mask);
				 const auto &[criteria, indirectDrawCommandBuffer] : std::ranges::subrange(begin, end)) {
				if (!resourceBindingState.boundPipeline.holds_alternative<std::remove_cvref_t<decltype(maskedRenderer)>>()) {
					maskedRenderer.bindPipeline(cb);
					resourceBindingState.boundPipeline.emplace<std::remove_cvref_t<decltype(maskedRenderer)>>();
				}

				if (!resourceBindingState.sceneDescriptorSetBound) {
					cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *maskedRenderer.pipelineLayout,
						0, { sharedData.sceneDescriptorSet, sharedData.assetDescriptorSet }, {});
					resourceBindingState.sceneDescriptorSetBound = true;
				}
				else if (!resourceBindingState.assetDescriptorSetBound) {
					// Scene descriptor set already bound by DepthRenderer, therefore binding only asset descriptor set (in set #1)
					// is enough.
					cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *maskedRenderer.pipelineLayout,
						1, sharedData.assetDescriptorSet, {});
					resourceBindingState.assetDescriptorSetBound = true;
				}

				if (!resourceBindingState.pushConstantBound) {
					maskedRenderer.pushConstants(cb, { projectionViewMatrix });
					resourceBindingState.pushConstantBound = true;
				}

				if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; resourceBindingState.cullMode != cullMode) {
					cb.setCullMode(resourceBindingState.cullMode.emplace(cullMode));
				}

				if (const auto &indexType = criteria.indexType) {
					if (resourceBindingState.indexBuffer != *indexType) {
						resourceBindingState.indexBuffer.emplace(*indexType);
						cb.bindIndexBuffer(indexBuffers.at(*indexType), 0, *indexType);
					}
					cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
				}
				else {
					cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
				}
			}
		};


	if (renderingNodes) {
		cb.beginRenderingKHR(passthruResources->depthPrepassAttachmentGroup.getRenderingInfo(
			vku::AttachmentGroup::ColorAttachmentInfo {
				// If cursor is not inside the passthru rect, mouse picking will not happen; node index attachment
				// doesn't have to be preserved.
				cursorPosFromPassthruRectTopLeft ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eDontCare,
				cursorPosFromPassthruRectTopLeft ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
				{ NO_INDEX, 0U, 0U, 0U },
			},
			vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));
		drawPrimitives(renderingNodes->indirectDrawCommandBuffers, sharedData.depthRenderer, sharedData.alphaMaskedDepthRenderer);
		cb.endRenderingKHR();
	}

	// Seeding jump flood initial image for hovering node.
	if (hoveringNode) {
		cb.beginRenderingKHR(passthruResources->hoveringNodeJumpFloodSeedAttachmentGroup.getRenderingInfo(
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
			vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));
		drawPrimitives(hoveringNode->indirectDrawCommandBuffers, sharedData.jumpFloodSeedRenderer, sharedData.alphaMaskedJumpFloodSeedRenderer);
		cb.endRenderingKHR();
	}

	// Seeding jump flood initial image for selected node.
	if (selectedNodes) {
		cb.beginRenderingKHR(passthruResources->selectedNodeJumpFloodSeedAttachmentGroup.getRenderingInfo(
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
			vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));
		drawPrimitives(selectedNodes->indirectDrawCommandBuffers, sharedData.jumpFloodSeedRenderer, sharedData.alphaMaskedJumpFloodSeedRenderer);
		cb.endRenderingKHR();
	}

	// If there are rendered nodes and the cursor is inside the passthru rect, do mouse picking.
	if (renderingNodes && cursorPosFromPassthruRectTopLeft) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
			{}, {}, {},
			// For copying to hoveringNodeIndexBuffer.
			vk::ImageMemoryBarrier {
				vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				passthruResources->depthPrepassAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
			});

		cb.copyImageToBuffer(
			passthruResources->depthPrepassAttachmentGroup.getColorAttachment(0).image, vk::ImageLayout::eTransferSrcOptimal,
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
	vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSet,
	std::uint32_t initialSampleOffset
) const -> bool {
	cb.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
		{}, {}, {},
		{
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eShaderRead,
				{}, vk::ImageLayout::eGeneral,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				passthruResources->hoveringNodeOutlineJumpFloodResources.image, vku::fullSubresourceRange(),
			},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eShaderRead,
				{}, vk::ImageLayout::eGeneral,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				passthruResources->selectedNodeOutlineJumpFloodResources.image, vku::fullSubresourceRange(),
			},
		});

	// Compute jump flood and get the last execution direction.
	return sharedData.jumpFloodComputer.compute(cb, descriptorSet, initialSampleOffset, vku::toExtent2D(image.extent));
}

auto vk_gltf_viewer::vulkan::Frame::recordSceneDrawCommands(vk::CommandBuffer cb) const -> void {
	assert(renderingNodes && "No nodes have to be rendered.");

	type_variant<std::monostate, PrimitiveRenderer, AlphaMaskedPrimitiveRenderer, FacetedPrimitiveRenderer, AlphaMaskedFacetedPrimitiveRenderer> boundPipeline{};
	std::optional<vk::CullModeFlagBits> currentCullMode{};
	std::optional<vk::IndexType> currentIndexBuffer{};

	// Both PrimitiveRenderer and AlphaMaskedPrimitiveRender have compatible descriptor set layouts and push constant range,
	// therefore they only need to be bound once.
	bool descriptorBound = false;
	bool pushConstantBound = false;

	// Render alphaMode=Opaque meshes.
	for (auto [begin, end] = renderingNodes->indirectDrawCommandBuffers.equal_range(fastgltf::AlphaMode::Opaque);
		 const auto &[criteria, indirectDrawCommandBuffer] : std::ranges::subrange(begin, end)) {
		if (criteria.faceted && !boundPipeline.holds_alternative<PrimitiveRenderer>()) {
			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.primitiveRenderer);
			boundPipeline.emplace<PrimitiveRenderer>();
		}
		else if (!criteria.faceted && !boundPipeline.holds_alternative<FacetedPrimitiveRenderer>()){
			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.facetedPrimitiveRenderer);
			boundPipeline.emplace<FacetedPrimitiveRenderer>();
		}
		if (!descriptorBound) {
			cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.sceneRenderingPipelineLayout, 0,
				{ sharedData.imageBasedLightingDescriptorSet, sharedData.assetDescriptorSet, sharedData.sceneDescriptorSet }, {});
			descriptorBound = true;
		}
		if (!pushConstantBound) {
			sharedData.sceneRenderingPipelineLayout.pushConstants(cb, { projectionViewMatrix, viewPosition });
			pushConstantBound = true;
		}

		if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; currentCullMode != cullMode) {
			cb.setCullMode(currentCullMode.emplace(cullMode));
		}

		if (const auto &indexType = criteria.indexType) {
			if (currentIndexBuffer != *indexType) {
				currentIndexBuffer.emplace(*indexType);
				cb.bindIndexBuffer(indexBuffers.at(*indexType), 0, *indexType);
			}
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}

	// Render alphaMode=Mask meshes.
	for (auto [begin, end] = renderingNodes->indirectDrawCommandBuffers.equal_range(fastgltf::AlphaMode::Mask);
		 const auto &[criteria, indirectDrawCommandBuffer] : std::ranges::subrange(begin, end)) {
		if (criteria.faceted && !boundPipeline.holds_alternative<AlphaMaskedPrimitiveRenderer>()) {
			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.alphaMaskedPrimitiveRenderer);
			boundPipeline.emplace<AlphaMaskedPrimitiveRenderer>();
		}
		else if (!criteria.faceted && !boundPipeline.holds_alternative<AlphaMaskedFacetedPrimitiveRenderer>()){
			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.alphaMaskedFacetedPrimitiveRenderer);
			boundPipeline.emplace<AlphaMaskedFacetedPrimitiveRenderer>();
		}
		if (!descriptorBound) {
			cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.sceneRenderingPipelineLayout, 0,
				{ sharedData.imageBasedLightingDescriptorSet, sharedData.assetDescriptorSet, sharedData.sceneDescriptorSet }, {});
			descriptorBound = true;
		}
		if (!pushConstantBound) {
			sharedData.sceneRenderingPipelineLayout.pushConstants(cb, { projectionViewMatrix, viewPosition });
			pushConstantBound = true;
		}

		if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; currentCullMode != cullMode) {
			cb.setCullMode(currentCullMode.emplace(cullMode));
		}

		if (const auto &indexType = criteria.indexType) {
			if (currentIndexBuffer != *indexType) {
				currentIndexBuffer.emplace(*indexType);
				cb.bindIndexBuffer(indexBuffers.at(*indexType), 0, *indexType);
			}
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}
}

auto vk_gltf_viewer::vulkan::Frame::recordSkyboxDrawCommands(vk::CommandBuffer cb) const -> void {
	assert(holds_alternative<vku::DescriptorSet<dsl::Skybox>>(background) && "recordSkyboxDrawCommand called, but background is not set to the proper skybox descriptor set.");
	sharedData.skyboxRenderer.draw(cb, get<vku::DescriptorSet<dsl::Skybox>>(background), { translationlessProjectionViewMatrix });
}

auto vk_gltf_viewer::vulkan::Frame::recordNodeOutlineCompositionCommands(
	vk::CommandBuffer cb,
	std::optional<bool> hoveringNodeJumpFloodForward,
	std::optional<bool> selectedNodeJumpFloodForward,
	std::uint32_t swapchainImageIndex
) const -> void {
	std::vector<vk::ImageMemoryBarrier> memoryBarriers; // TODO: use static_vector with capacity = 2.
	// Change jump flood image layouts to ShaderReadOnlyOptimal.
	if (hoveringNodeJumpFloodForward) {
		memoryBarriers.push_back({
			{}, vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			passthruResources->hoveringNodeOutlineJumpFloodResources.image,
			{ vk::ImageAspectFlagBits::eColor, 0, 1, *hoveringNodeJumpFloodForward, 1 },
		});
	}
	if (selectedNodeJumpFloodForward) {
		memoryBarriers.push_back({
			{}, vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			passthruResources->selectedNodeOutlineJumpFloodResources.image,
			{ vk::ImageAspectFlagBits::eColor, 0, 1, *selectedNodeJumpFloodForward, 1 },
		});
	}
	if (!memoryBarriers.empty()) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader,
			{}, {}, {}, memoryBarriers);
	}

	// Set viewport and scissor.
	const vk::Viewport passthruViewport {
		// Use negative viewport.
		static_cast<float>(passthruRect.offset.x), static_cast<float>(passthruRect.offset.y + passthruRect.extent.height),
		static_cast<float>(passthruRect.extent.width), -static_cast<float>(passthruRect.extent.height),
		0.f, 1.f,
	};
	cb.setViewport(0, passthruViewport);
	cb.setScissor(0, passthruRect);

	cb.beginRenderingKHR(sharedData.swapchainAttachmentGroup.getRenderingInfo(
		vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore },
		swapchainImageIndex));

	// Draw hovering/selected node outline if exists.
	bool pipelineBound = false;
	if (selectedNodes) {
		if (!pipelineBound) {
			sharedData.outlineRenderer.bindPipeline(cb);
			pipelineBound = true;
		}
		sharedData.outlineRenderer.bindDescriptorSets(cb, selectedNodeOutlineSet);
		sharedData.outlineRenderer.pushConstants(cb, {
			.outlineColor = selectedNodes->outlineColor,
			.passthruOffset = { passthruRect.offset.x, passthruRect.offset.y },
			.outlineThickness = selectedNodes->outlineThickness,
		});
		sharedData.outlineRenderer.draw(cb);
	}
	if (hoveringNode) {
		if (selectedNodes) {
			// TODO: pipeline barrier required.
		}

		if (!pipelineBound) {
			sharedData.outlineRenderer.bindPipeline(cb);
			pipelineBound = true;
		}

		sharedData.outlineRenderer.bindDescriptorSets(cb, hoveringNodeOutlineSet);
		sharedData.outlineRenderer.pushConstants(cb, {
			.outlineColor = hoveringNode->outlineColor,
			.passthruOffset = { passthruRect.offset.x, passthruRect.offset.y },
			.outlineThickness = hoveringNode->outlineThickness,
		});
		sharedData.outlineRenderer.draw(cb);
	}

    cb.endRenderingKHR();
}

auto vk_gltf_viewer::vulkan::Frame::recordImGuiCompositionCommands(
	vk::CommandBuffer cb,
	std::uint32_t swapchainImageIndex
) const -> void {
	// Start dynamic rendering with B8G8R8A8_UNORM format.
	cb.beginRenderingKHR(sharedData.imGuiSwapchainAttachmentGroup.getRenderingInfo(
		vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore },
		swapchainImageIndex));

	// Draw ImGui.
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

    cb.endRenderingKHR();
}