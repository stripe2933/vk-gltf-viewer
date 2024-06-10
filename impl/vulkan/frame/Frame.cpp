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
import :gltf;
import :vulkan.frame.Frame;
import :helpers.ranges;

vk_gltf_viewer::vulkan::Frame::Frame(
	const Gpu &gpu,
	const std::shared_ptr<SharedData> &sharedData
) : sharedData { sharedData },
	primaryAttachmentGroup { createPrimaryAttachmentGroup(gpu) },
    descriptorPool { createDescriptorPool(gpu.device) },
	graphicsCommandPool { createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent) },
	cameraBuffer { createCameraBuffer(gpu.allocator) },
	meshRendererSets { *gpu.device, *descriptorPool, sharedData->meshRenderer.descriptorSetLayouts },
	swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} },
	drawFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	blitToSwapchainFinishSema { gpu.device, vk::SemaphoreCreateInfo{} },
	inFlightFence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } } {
	// Update per-frame descriptor sets.
	gpu.device.updateDescriptorSets(
	    ranges::array_cat(
		    meshRendererSets.getDescriptorWrites0({ cameraBuffer, 0, vk::WholeSize }).get(),
		    meshRendererSets.getDescriptorWrites1(
				sharedData->assetResources.textures,
		    	{ sharedData->assetResources.materialBuffer, 0, vk::WholeSize }).get(),
		    meshRendererSets.getDescriptorWrites2(
		    	{ sharedData->sceneResources.nodeTransformBuffer, 0, vk::WholeSize }).get()),
		{});

	// Allocate per-frame command buffers.
	std::tie(drawCommandBuffer, blitToSwapchainCommandBuffer)
	    = (*gpu.device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
	    	*graphicsCommandPool,
	    	vk::CommandBufferLevel::ePrimary,
	    	2,
	    })
		| ranges::to_array<2>();

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
	draw(drawCommandBuffer);
	blitToSwapchain(blitToSwapchainCommandBuffer, sharedData->swapchainAttachmentGroups[imageIndex]);

	// Submit draw command to the graphics queue.
	const std::array waitSemas { *swapchainImageAcquireSema, *drawFinishSema };
	constexpr std::array waitStages { vku::toFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput), vku::toFlags(vk::PipelineStageFlagBits::eTransfer) };
	gpu.queues.graphicsPresent.submit(std::array {
		vk::SubmitInfo {
			{},
			{},
			drawCommandBuffer,
			*drawFinishSema,
		},
		vk::SubmitInfo {
			waitSemas,
			waitStages,
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
	primaryAttachmentGroup = createPrimaryAttachmentGroup(gpu);
	initAttachmentLayouts(gpu);
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
    	vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, static_cast<std::uint32_t>(sharedData->assetResources.textures.size()) },
    	vk::DescriptorPoolSize { vk::DescriptorType::eStorageBuffer, 2 },
    	vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 1 },
    };
	return { device, vk::DescriptorPoolCreateInfo{
		vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
		3,
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
	return { allocator, MeshRenderer::Camera{}, vk::BufferUsageFlagBits::eUniformBuffer };
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
	constexpr glm::vec3 viewPosition { 0.5f };
	const MeshRenderer::Camera camera {
		glm::gtc::perspective(glm::radians(45.0f), vku::aspect(sharedData->swapchainExtent), 0.1f, 100.0f)
		    * glm::gtc::lookAt(viewPosition, glm::vec3{ 0.f }, glm::vec3{ 0.f, 1.f, 0.f }),
		viewPosition,
	};
	cameraBuffer.asValue<MeshRenderer::Camera>() = camera;
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
			std::tuple { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::ClearColorValue{} },
		},
		std::tuple { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::ClearDepthStencilValue { 1.f, 0U } }));

	// Set viewport and scissor.
	primaryAttachmentGroup.setViewport(cb, true);
	primaryAttachmentGroup.setScissor(cb);

	// Draw glTF mesh.
	sharedData->meshRenderer.bindPipeline(cb);
	sharedData->meshRenderer.bindDescriptorSets(cb, meshRendererSets);

	// Collect glTF mesh primitives.
	std::vector<std::tuple<std::size_t /* nodeIndex */, std::size_t /* materialIndex */, const gltf::AssetResources::PrimitiveData*>> primitives;
	for (std::stack dfs { std::from_range, sharedData->asset.scenes[sharedData->asset.defaultScene.value_or(0)].nodeIndices | std::views::reverse }; !dfs.empty(); ) {
		const std::size_t nodeIndex = dfs.top();
        const fastgltf::Node &node = sharedData->asset.nodes[nodeIndex];
        if (node.meshIndex) {
        	const fastgltf::Mesh &mesh = sharedData->asset.meshes[*node.meshIndex];
        	primitives.append_range(
        		mesh.primitives | std::views::transform([&](const fastgltf::Primitive &primitive) {
					const gltf::AssetResources::PrimitiveData &primitiveData = sharedData->assetResources.primitiveData.at(&primitive);
					return std::tuple { nodeIndex, primitive.materialIndex.value(), &primitiveData };
				}));
        }
		dfs.pop();
		dfs.push_range(node.children | std::views::reverse);
	}

	// Sort primitive by index type.
	constexpr auto getIndexType = [](const auto &primitive) {
		return get<2>(primitive)->indexInfo
			.transform([](const auto &info) { return info.type; });
	};
	std::ranges::sort(primitives, {}, getIndexType);
	for (auto primitivesWithSameIndexType : std::views::chunk_by(primitives, [&](const auto &lhs, const auto &rhs) { return getIndexType(lhs) == getIndexType(rhs); })) {
		constexpr auto value_or = []<typename Key, typename Value>(const std::unordered_map<Key, Value> &map, const Key &key, Value default_value) {
			if (auto it = map.find(key); it != map.end()) return it->second;
			else return default_value;
		};

		if (std::optional indexType = getIndexType(primitivesWithSameIndexType.front()); indexType) {
			const std::size_t indexByteSize = [=]() {
				switch (*indexType) {
					case vk::IndexType::eUint16: return sizeof(std::uint16_t);
					case vk::IndexType::eUint32: return sizeof(std::uint32_t);
					default: throw std::runtime_error{ "Unsupported index type: only Uint16 and Uint32 are supported" };
				};
			}();
			cb.bindIndexBuffer(sharedData->assetResources.indexBuffers.at(*indexType), 0, *indexType);

			for (const auto [nodeIndex, materialIndex, pPrimitiveData] : primitivesWithSameIndexType) {
				sharedData->meshRenderer.pushConstants(cb, {
					.pPositionBuffer = pPrimitiveData->positionInfo.address,
					.pNormalBuffer = pPrimitiveData->normalInfo.value().address,
					.pTangentBuffer = pPrimitiveData->tangentInfo.value().address,
					.pTexcoordBuffers = pPrimitiveData->pTexcoordReferenceBuffer,
					.pColorBuffers = pPrimitiveData->pColorReferenceBuffer,
					.positionFloatStride = static_cast<std::uint8_t>(pPrimitiveData->positionInfo.byteStride / sizeof(float)),
					.normalFloatStride = static_cast<std::uint8_t>(pPrimitiveData->normalInfo.value().byteStride / sizeof(float)),
					.tangentFloatStride = static_cast<std::uint8_t>(pPrimitiveData->tangentInfo.value().byteStride / sizeof(float)),
					.pTexcoordFloatStrideBuffer = pPrimitiveData->pTexcoordFloatStrideBuffer,
					.pColorFloatStrideBuffer = pPrimitiveData->pColorFloatStrideBuffer,
					.nodeIndex = static_cast<std::uint32_t>(nodeIndex),
					.materialIndex = static_cast<std::uint32_t>(materialIndex),
			    });
				cb.drawIndexed(
					pPrimitiveData->drawCount, 1,
					pPrimitiveData->indexInfo->offset / indexByteSize, 0, 0);
			}
		}
		else {
			for (const auto [nodeIndex, materialIndex, pPrimitiveData] : primitivesWithSameIndexType) {
				sharedData->meshRenderer.pushConstants(cb, {
					.pPositionBuffer = pPrimitiveData->positionInfo.address,
					.pNormalBuffer = pPrimitiveData->normalInfo.value().address,
					.pTangentBuffer = pPrimitiveData->tangentInfo.value().address,
					.pTexcoordBuffers = pPrimitiveData->pTexcoordReferenceBuffer,
					.pColorBuffers = pPrimitiveData->pColorReferenceBuffer,
					.positionFloatStride = static_cast<std::uint8_t>(pPrimitiveData->positionInfo.byteStride / sizeof(float)),
					.normalFloatStride = static_cast<std::uint8_t>(pPrimitiveData->normalInfo.value().byteStride / sizeof(float)),
					.tangentFloatStride = static_cast<std::uint8_t>(pPrimitiveData->tangentInfo.value().byteStride / sizeof(float)),
					.pTexcoordFloatStrideBuffer = pPrimitiveData->pTexcoordFloatStrideBuffer,
					.pColorFloatStrideBuffer = pPrimitiveData->pColorFloatStrideBuffer,
					.nodeIndex = static_cast<std::uint32_t>(nodeIndex),
					.materialIndex = static_cast<std::uint32_t>(materialIndex),
				});
				cb.draw(pPrimitiveData->drawCount, 1, 0, 0);
			}
		}
	}

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