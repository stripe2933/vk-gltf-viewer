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

vk_gltf_viewer::vulkan::Frame::Frame(
	const Gpu &gpu,
	const SharedData &sharedData
) : gpu { gpu },
    hoveringNodeIndexBuffer { gpu.allocator, NO_INDEX, vk::BufferUsageFlagBits::eTransferDst, vku::allocation::hostRead },
	sharedData { sharedData } {
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
					sceneMsaaImage, vku::fullSubresourceRange(),
				},
				vk::ImageMemoryBarrier {
					{}, {},
					{}, vk::ImageLayout::eDepthAttachmentOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					sceneDepthImage, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth),
				},
			});
	});

	// Allocate descriptor sets.
	std::tie(hoveringNodeJumpFloodSets, selectedNodeJumpFloodSets, hoveringNodeOutlineSets, selectedNodeOutlineSets)
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

auto vk_gltf_viewer::vulkan::Frame::execute(
	const ExecutionTask &task
) -> std::expected<ExecutionResult, ExecutionError> {
	constexpr std::uint64_t MAX_TIMEOUT = std::numeric_limits<std::uint64_t>::max();

	// Wait for the previous frame execution to finish.
	if (auto result = gpu.device.waitForFences(*inFlightFence, true, MAX_TIMEOUT); result != vk::Result::eSuccess) {
		throw std::runtime_error{ std::format("Failed to wait for in-flight fence: {}", to_string(result)) };
	}
	gpu.device.resetFences(*inFlightFence);

	ExecutionResult result{};

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
		return std::unexpected { ExecutionError::SwapchainAcquireFailed };
	}

	// Record commands.
	graphicsCommandPool.reset();
	computeCommandPool.reset();

	// Depth prepass and jump flood seed image calculation pass.
	{
		scenePrepassCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		if (task.gltf) {
			recordScenePrepassCommands(scenePrepassCommandBuffer, task);
		}
		scenePrepassCommandBuffer.end();
	}

	// Jump flood calculation pass.
	// TODO: If there are multiple compute queues, distribute the tasks to avoid the compute pipeline stalling.
	std::optional<bool> hoveringNodeJumpFloodForward{}, selectedNodeJumpFloodForward{};
	{
		jumpFloodCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		if (task.gltf) {
			if (hoveringNodeIndex && task.hoveringNodeOutline) {
				hoveringNodeJumpFloodForward = recordJumpFloodComputeCommands(
					jumpFloodCommandBuffer,
					passthruResources->hoveringNodeOutlineJumpFloodResources.image,
					hoveringNodeJumpFloodSets,
					std::bit_ceil(static_cast<std::uint32_t>(task.hoveringNodeOutline->thickness)));
				gpu.device.updateDescriptorSets(
					hoveringNodeOutlineSets.getWriteOne<0>({
						{},
						*hoveringNodeJumpFloodForward
							? *passthruResources->hoveringNodeOutlineJumpFloodResources.pongImageView
							: *passthruResources->hoveringNodeOutlineJumpFloodResources.pingImageView,
						vk::ImageLayout::eShaderReadOnlyOptimal,
					}),
					{});
			}
			if (!selectedNodeIndices.empty() && task.selectedNodeOutline) {
				selectedNodeJumpFloodForward = recordJumpFloodComputeCommands(
					jumpFloodCommandBuffer,
					passthruResources->selectedNodeOutlineJumpFloodResources.image,
					selectedNodeJumpFloodSets,
					std::bit_ceil(static_cast<std::uint32_t>(task.selectedNodeOutline->thickness)));
				gpu.device.updateDescriptorSets(
					selectedNodeOutlineSets.getWriteOne<0>({
						{},
						*selectedNodeJumpFloodForward
							? *passthruResources->selectedNodeOutlineJumpFloodResources.pongImageView
							: *passthruResources->selectedNodeOutlineJumpFloodResources.pingImageView,
						vk::ImageLayout::eShaderReadOnlyOptimal,
					}),
					{});
			}
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
		if (auto clearColor = get_if<glm::vec3>(&task.background)) {
			backgroundColor.setFloat32({ clearColor->x, clearColor->y, clearColor->z, 1.f });
		}
		sceneRenderingCommandBuffer.beginRenderingKHR(sceneAttachmentGroups[imageIndex].getRenderingInfo(
			std::array {
				vku::MsaaAttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, backgroundColor },
			},
			vku::MsaaAttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));

		const vk::Viewport passthruViewport {
			// Use negative viewport.
			static_cast<float>(task.passthruRect.offset.x), static_cast<float>(task.passthruRect.offset.y + task.passthruRect.extent.height),
			static_cast<float>(task.passthruRect.extent.width), -static_cast<float>(task.passthruRect.extent.height),
			0.f, 1.f,
		};
		sceneRenderingCommandBuffer.setViewport(0, passthruViewport);
		sceneRenderingCommandBuffer.setScissor(0, task.passthruRect);

		if (task.gltf) {
			recordSceneDrawCommands(sceneRenderingCommandBuffer, task);
		}
		if (holds_alternative<vku::DescriptorSet<dsl::Skybox>>(task.background)) {
			recordSkyboxDrawCommands(sceneRenderingCommandBuffer, task);
		}

		sceneRenderingCommandBuffer.endRenderingKHR();

		sceneRenderingCommandBuffer.end();
	}

	// Post-composition pass.
	{
		compositionCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		if (task.gltf) {
			recordNodeOutlineCompositionCommands(compositionCommandBuffer, hoveringNodeJumpFloodForward, selectedNodeJumpFloodForward, imageIndex, task);

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
			*swapchainImageAcquireSema,
			vku::unsafeProxy(vk::Flags { vk::PipelineStageFlagBits::eColorAttachmentOutput }),
			sceneRenderingCommandBuffer,
			*drawFinishSema,
		},
		vk::SubmitInfo {
			vku::unsafeProxy({ *drawFinishSema, *jumpFloodFinishSema }),
			vku::unsafeProxy({
				vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
				vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
			}),
			compositionCommandBuffer,
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
	pingImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) },
	pongImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 }) } { }

auto vk_gltf_viewer::vulkan::Frame::handleSwapchainResize(
	vk::SurfaceKHR surface,
	const vk::Extent2D &newExtent
) -> void {
	sceneMsaaImage = createSceneMsaaImage();
	sceneDepthImage = createSceneDepthImage();
	sceneAttachmentGroups = createSceneAttachmentGroups();
}

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

auto vk_gltf_viewer::vulkan::Frame::createSceneMsaaImage() const -> vku::AllocatedImage {
	return { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eB8G8R8A8Srgb,
		vk::Extent3D { sharedData.swapchainExtent, 1 },
		1, 1,
		vk::SampleCountFlagBits::e4,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
	}, vku::allocation::deviceLocalTransient };
}

auto vk_gltf_viewer::vulkan::Frame::createSceneDepthImage() const -> vku::AllocatedImage {
	return { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eD32Sfloat,
		vk::Extent3D { sharedData.swapchainExtent, 1 },
		1, 1,
		vk::SampleCountFlagBits::e4,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
	}, vku::allocation::deviceLocalTransient };
}

auto vk_gltf_viewer::vulkan::Frame::createSceneAttachmentGroups() const -> std::vector<ag::Scene> {
	return sharedData.swapchainImages
		| std::views::transform([this](vk::Image swapchainImage) {
			return ag::Scene {
				gpu.device,
				sceneMsaaImage,
				vku::Image { swapchainImage, vk::Extent3D { sharedData.swapchainExtent, 1 }, vk::Format::eB8G8R8A8Srgb, 1, 1 },
				sceneDepthImage,
			};
		})
		| std::ranges::to<std::vector>();
}

auto vk_gltf_viewer::vulkan::Frame::createDescriptorPool() const -> decltype(descriptorPool) {
	return {
		gpu.device,
		(2 * getPoolSizes(sharedData.jumpFloodComputer.descriptorSetLayout, sharedData.outlineRenderer.descriptorSetLayout))
			.getDescriptorPoolCreateInfo(),
	};
}

auto vk_gltf_viewer::vulkan::Frame::update(
    const ExecutionTask &task,
	ExecutionResult &result
) -> void {
	// Get node index under the cursor from hoveringNodeIndexBuffer.
	// If it is not NO_INDEX (i.e. node index is found), update hoveringNodeIndex.
	if (auto value = std::exchange(hoveringNodeIndexBuffer.asValue<std::uint32_t>(), NO_INDEX); value != NO_INDEX) {
		result.hoveringNodeIndex = value;
	}

	if (task.gltf) {
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

		if (renderingNodeIndices != task.gltf->renderingNodeIndices) {
			renderingNodeIndices = std::move(task.gltf->renderingNodeIndices);
			renderingNodeIndirectDrawCommandBuffers = task.gltf->sceneResources.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, renderingNodeIndices);
		}
		if (hoveringNodeIndex != task.gltf->hoveringNodeIndex) {
			hoveringNodeIndex = task.gltf->hoveringNodeIndex;
			hoveringNodeIndirectDrawCommandBuffers = task.gltf->sceneResources.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, { *hoveringNodeIndex });
		}
		if (selectedNodeIndices != task.gltf->selectedNodeIndices) {
			selectedNodeIndices = task.gltf->selectedNodeIndices;
			selectedNodeIndirectDrawCommandBuffers = task.gltf->sceneResources.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, selectedNodeIndices);
		}

		// If passthru extent is different from the current's, dependent images have to be recreated.
		if (!passthruExtent || *passthruExtent != task.passthruRect.extent) {
			passthruExtent.emplace(task.passthruRect.extent);
			vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
				passthruResources.emplace(gpu, *passthruExtent, cb);
			});
			gpu.queues.graphicsPresent.waitIdle(); // TODO: idling while frame execution is very inefficient.

			gpu.device.updateDescriptorSets({
				hoveringNodeJumpFloodSets.getWrite<0>(vku::unsafeProxy({
					vk::DescriptorImageInfo { {}, *passthruResources->hoveringNodeOutlineJumpFloodResources.pingImageView, vk::ImageLayout::eGeneral },
					vk::DescriptorImageInfo { {}, *passthruResources->hoveringNodeOutlineJumpFloodResources.pongImageView, vk::ImageLayout::eGeneral },
				})),
				selectedNodeJumpFloodSets.getWrite<0>(vku::unsafeProxy({
					vk::DescriptorImageInfo { {}, *passthruResources->selectedNodeOutlineJumpFloodResources.pingImageView, vk::ImageLayout::eGeneral },
					vk::DescriptorImageInfo { {}, *passthruResources->selectedNodeOutlineJumpFloodResources.pongImageView, vk::ImageLayout::eGeneral },
				})),
			}, {});
		}
	}
}

auto vk_gltf_viewer::vulkan::Frame::recordScenePrepassCommands(
	vk::CommandBuffer cb,
	const ExecutionTask &task
) const -> void {
	std::vector memoryBarriers {
		// Regardless whether mouse cursor is inside the passthru rect or not, attachment layout transition depth
		// prepass must be done (for future use).
		vk::ImageMemoryBarrier {
			{}, vk::AccessFlagBits::eColorAttachmentWrite,
			{}, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			passthruResources->depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
		},
	};

	// If hovering node's outline have to be rendered, prepare attachment layout transition for jump flood seeding.
	const auto addJumpFloodSeedImageMemoryBarrier = [&](vk::Image image) {
		memoryBarriers.push_back({
			{}, vk::AccessFlagBits::eColorAttachmentWrite,
			{}, vk::ImageLayout::eColorAttachmentOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
		});
	};
	if (hoveringNodeIndex && task.hoveringNodeOutline) {
		addJumpFloodSeedImageMemoryBarrier(passthruResources->hoveringNodeOutlineJumpFloodResources.image);
	}
	// Same holds for selected nodes' outline.
	if (!selectedNodeIndices.empty() && task.selectedNodeOutline) {
		addJumpFloodSeedImageMemoryBarrier(passthruResources->selectedNodeOutlineJumpFloodResources.image);
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
						cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *opaqueOrBlendRenderer.pipelineLayout, 0, task.gltf->sceneDescriptorSet, {});
						resourceBindingState.sceneDescriptorSetBound = true;
					}

					if (!resourceBindingState.pushConstantBound) {
						opaqueOrBlendRenderer.pushConstants(cb, {
							task.camera.projection * task.camera.view,
						});
						resourceBindingState.pushConstantBound = true;
					}

					if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; resourceBindingState.cullMode != cullMode) {
						cb.setCullMode(resourceBindingState.cullMode.emplace(cullMode));
					}

					if (const auto &indexType = criteria.indexType) {
						if (resourceBindingState.indexBuffer != *indexType) {
							resourceBindingState.indexBuffer.emplace(*indexType);
							cb.bindIndexBuffer(task.gltf->indexBuffers.at(*indexType), 0, *indexType);
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
						0, { task.gltf->sceneDescriptorSet, task.gltf->assetDescriptorSet }, {});
					resourceBindingState.sceneDescriptorSetBound = true;
				}
				else if (!resourceBindingState.assetDescriptorSetBound) {
					// Scene descriptor set already bound by DepthRenderer, therefore binding only asset descriptor set (in set #1)
					// is enough.
					cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *maskedRenderer.pipelineLayout,
						1, task.gltf->assetDescriptorSet, {});
					resourceBindingState.assetDescriptorSetBound = true;
				}

				if (!resourceBindingState.pushConstantBound) {
					maskedRenderer.pushConstants(cb, {
						task.camera.projection * task.camera.view,
					});
					resourceBindingState.pushConstantBound = true;
				}

				if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; resourceBindingState.cullMode != cullMode) {
					cb.setCullMode(resourceBindingState.cullMode.emplace(cullMode));
				}

				if (const auto &indexType = criteria.indexType) {
					if (resourceBindingState.indexBuffer != *indexType) {
						resourceBindingState.indexBuffer.emplace(*indexType);
						cb.bindIndexBuffer(task.gltf->indexBuffers.at(*indexType), 0, *indexType);
					}
					cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
				}
				else {
					cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
				}
			}
		};

	const auto cursorPosFromPassthruRectTopLeft
		= task.mouseCursorOffset.transform([&](const vk::Offset2D &offset) {
			return vk::Offset2D { offset.x - task.passthruRect.offset.x, offset.y - task.passthruRect.offset.y };
		});
	const bool isCursorInPassthruRect
		= cursorPosFromPassthruRectTopLeft
		.transform([&](const vk::Offset2D &offset) {
			return 0 <= offset.x && offset.x < task.passthruRect.extent.width
			    && 0 <= offset.y && offset.y < task.passthruRect.extent.height;
		})
		.value_or(false);

	cb.beginRenderingKHR(passthruResources->depthPrepassAttachmentGroup.getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { isCursorInPassthruRect ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eDontCare, isCursorInPassthruRect ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare, { NO_INDEX, 0U, 0U, 0U } },
		},
		vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));
	drawPrimitives(renderingNodeIndirectDrawCommandBuffers, sharedData.depthRenderer, sharedData.alphaMaskedDepthRenderer);
	cb.endRenderingKHR();

	// Seeding jump flood initial image for hovering node.
	if (hoveringNodeIndex && task.hoveringNodeOutline) {
		cb.beginRenderingKHR(passthruResources->hoveringNodeJumpFloodSeedAttachmentGroup.getRenderingInfo(
			std::array {
				vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
			},
			vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));
		drawPrimitives(hoveringNodeIndirectDrawCommandBuffers, sharedData.jumpFloodSeedRenderer, sharedData.alphaMaskedJumpFloodSeedRenderer);
		cb.endRenderingKHR();
	}

	// Seeding jump flood initial image for selected node.
	if (!selectedNodeIndices.empty() && task.selectedNodeOutline) {
		cb.beginRenderingKHR(passthruResources->selectedNodeJumpFloodSeedAttachmentGroup.getRenderingInfo(
			std::array {
				vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
			},
			vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));
		drawPrimitives(selectedNodeIndirectDrawCommandBuffers, sharedData.jumpFloodSeedRenderer, sharedData.alphaMaskedJumpFloodSeedRenderer);
		cb.endRenderingKHR();
	}

	// If cursor is in the passthru rect, do mouse picking.
	if (isCursorInPassthruRect) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
			{}, {}, {},
			// For copying to hoveringNodeIndexBuffer.
			vk::ImageMemoryBarrier {
				vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				passthruResources->depthPrepassAttachmentGroup.colorAttachments[0].image, vku::fullSubresourceRange(),
			});

		cb.copyImageToBuffer(
			passthruResources->depthPrepassAttachmentGroup.colorAttachments[0].image, vk::ImageLayout::eTransferSrcOptimal,
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
	vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSets,
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
	return sharedData.jumpFloodComputer.compute(cb, descriptorSets, initialSampleOffset, vku::toExtent2D(image.extent));
}

auto vk_gltf_viewer::vulkan::Frame::recordSceneDrawCommands(
	vk::CommandBuffer cb,
	const ExecutionTask &task
) const -> void {
	assert(task.gltf && "Drawing glTF scene requested, but no rendering info (task.gltf) is available.");

	type_variant<std::monostate, PrimitiveRenderer, AlphaMaskedPrimitiveRenderer, FacetedPrimitiveRenderer, AlphaMaskedFacetedPrimitiveRenderer> boundPipeline{};
	std::optional<vk::CullModeFlagBits> currentCullMode{};
	std::optional<vk::IndexType> currentIndexBuffer{};

	// Both PrimitiveRenderer and AlphaMaskedPrimitiveRender have compatible descriptor set layouts and push constant range,
	// therefore they only need to be bound once.
	bool descriptorBound = false;
	bool pushConstantBound = false;

	// Render alphaMode=Opaque meshes.
	for (auto [begin, end] = renderingNodeIndirectDrawCommandBuffers.equal_range(fastgltf::AlphaMode::Opaque);
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
				{ task.imageBasedLightingDescriptorSet, task.gltf->assetDescriptorSet, task.gltf->sceneDescriptorSet }, {});
			descriptorBound = true;
		}
		if (!pushConstantBound) {
			sharedData.sceneRenderingPipelineLayout.pushConstants(cb, { task.camera.projection * task.camera.view, inverse(task.camera.view)[3] });
			pushConstantBound = true;
		}

		if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; currentCullMode != cullMode) {
			cb.setCullMode(currentCullMode.emplace(cullMode));
		}

		if (const auto &indexType = criteria.indexType) {
			if (currentIndexBuffer != *indexType) {
				currentIndexBuffer.emplace(*indexType);
				cb.bindIndexBuffer(task.gltf->indexBuffers.at(*indexType), 0, *indexType);
			}
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}

	// Render alphaMode=Mask meshes.
	for (auto [begin, end] = renderingNodeIndirectDrawCommandBuffers.equal_range(fastgltf::AlphaMode::Mask);
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
				{ task.imageBasedLightingDescriptorSet, task.gltf->assetDescriptorSet, task.gltf->sceneDescriptorSet }, {});
			descriptorBound = true;
		}
		if (!pushConstantBound) {
			sharedData.sceneRenderingPipelineLayout.pushConstants(cb, { task.camera.projection * task.camera.view, inverse(task.camera.view)[3] });
			pushConstantBound = true;
		}

		if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; currentCullMode != cullMode) {
			cb.setCullMode(currentCullMode.emplace(cullMode));
		}

		if (const auto &indexType = criteria.indexType) {
			if (currentIndexBuffer != *indexType) {
				currentIndexBuffer.emplace(*indexType);
				cb.bindIndexBuffer(task.gltf->indexBuffers.at(*indexType), 0, *indexType);
			}
			cb.drawIndexedIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndexedIndirectCommand), sizeof(vk::DrawIndexedIndirectCommand));
		}
		else {
			cb.drawIndirect(indirectDrawCommandBuffer, 0, indirectDrawCommandBuffer.size / sizeof(vk::DrawIndirectCommand), sizeof(vk::DrawIndirectCommand));
		}
	}
}


auto vk_gltf_viewer::vulkan::Frame::recordSkyboxDrawCommands(
	vk::CommandBuffer cb,
	const ExecutionTask &task
) const -> void {
	assert(holds_alternative<vku::DescriptorSet<dsl::Skybox>>(task.background)
		&& "Skybox drawing requested, but no skybox descriptor set (task.background with alternative type=vku::DescriptorSet<dsl::Skybox>) is available.");

	// Draw skybox.
	const glm::mat4 noTranslationView = { glm::mat3 { task.camera.view } };
	sharedData.skyboxRenderer.draw(cb, get<vku::DescriptorSet<dsl::Skybox>>(task.background), {
		task.camera.projection * noTranslationView,
	});
}

auto vk_gltf_viewer::vulkan::Frame::recordNodeOutlineCompositionCommands(
	vk::CommandBuffer cb,
	std::optional<bool> hoveringNodeJumpFloodForward,
	std::optional<bool> selectedNodeJumpFloodForward,
	std::uint32_t swapchainImageIndex,
	const ExecutionTask &task
) const -> void {
	std::vector<vk::ImageMemoryBarrier> memoryBarriers;
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
		static_cast<float>(task.passthruRect.offset.x), static_cast<float>(task.passthruRect.offset.y + task.passthruRect.extent.height),
		static_cast<float>(task.passthruRect.extent.width), -static_cast<float>(task.passthruRect.extent.height),
		0.f, 1.f,
	};
	cb.setViewport(0, passthruViewport);
	cb.setScissor(0, task.passthruRect);

	cb.beginRenderingKHR(sharedData.swapchainAttachmentGroups[swapchainImageIndex].getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore }
		}));

	// Draw hovering/selected node outline if exists.
	bool pipelineBound = false;
	if (!selectedNodeIndices.empty() && task.selectedNodeOutline) {
		if (!pipelineBound) {
			sharedData.outlineRenderer.bindPipeline(cb);
			pipelineBound = true;
		}
		sharedData.outlineRenderer.bindDescriptorSets(cb, selectedNodeOutlineSets);
		sharedData.outlineRenderer.pushConstants(cb, {
			.outlineColor = task.selectedNodeOutline->color,
			.passthruOffset = { task.passthruRect.offset.x, task.passthruRect.offset.y },
			.outlineThickness = task.selectedNodeOutline->thickness,
		});
		sharedData.outlineRenderer.draw(cb);
	}
	if (hoveringNodeIndex && task.hoveringNodeOutline &&
		(!selectedNodeIndices.contains(*hoveringNodeIndex) /* If mouse is hovering over the selected nodes, hovering node outline doesn't have to be rendered. */ ||
			!task.selectedNodeOutline /* If selected node outline rendering is disabled, hovering node should be rendered even if it is contained in the selectedNodeIndices. */)) {
		if (!selectedNodeIndices.empty() && task.selectedNodeOutline) {
			// TODO: pipeline barrier required.
		}

		if (!pipelineBound) {
			sharedData.outlineRenderer.bindPipeline(cb);
			pipelineBound = true;
		}

		sharedData.outlineRenderer.bindDescriptorSets(cb, hoveringNodeOutlineSets);
		sharedData.outlineRenderer.pushConstants(cb, {
			.outlineColor = task.hoveringNodeOutline->color,
			.passthruOffset = { task.passthruRect.offset.x, task.passthruRect.offset.y },
			.outlineThickness = task.hoveringNodeOutline->thickness,
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
	cb.beginRenderingKHR(sharedData.imGuiSwapchainAttachmentGroups[swapchainImageIndex].getRenderingInfo(
		std::array {
			vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore }
		}));

	// Draw ImGui.
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

    cb.endRenderingKHR();
}