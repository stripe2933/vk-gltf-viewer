module;

#include <fastgltf/core.hpp>
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.SharedData;

import std;
import pbrenvmap;
import :helpers.ranges;
import :mipmap;
import :vulkan.pipelines.BrdfmapComputer;

vk_gltf_viewer::vulkan::SharedData::SharedData(
    const fastgltf::Asset &asset,
    const std::filesystem::path &assetDir,
    const Gpu &gpu,
    vk::SurfaceKHR surface,
	const vk::Extent2D &swapchainExtent,
	const vku::Image &eqmapImage
) : asset { asset },
	gpu { gpu },
	assetResources { asset, assetDir, gpu, { .supportUint8Index = false /* TODO: change this value depend on vk::PhysicalDeviceIndexTypeUint8FeaturesKHR */ } },
	swapchain { createSwapchain(surface, swapchainExtent) },
	swapchainExtent { swapchainExtent } {
	{
		// Create image view for eqmapImage.
		const vk::raii::ImageView eqmapImageView { gpu.device, eqmapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) };

		const pbrenvmap::Generator::Pipelines pbrenvmapPipelines {
			.cubemapComputer = { gpu.device, compiler },
			.subgroupMipmapComputer = { gpu.device, vku::Image::maxMipLevels(1024U), 32U /* TODO */, compiler },
			.sphericalHarmonicsComputer = { gpu.device, compiler },
			.sphericalHarmonicCoefficientsSumComputer = { gpu.device, compiler },
			.prefilteredmapComputer = { gpu.device, { vku::Image::maxMipLevels(256U), 1024 }, compiler },
			.multiplyComputer = { gpu.device, compiler },
		};
		pbrenvmap::Generator pbrenvmapGenerator { gpu.device, gpu.allocator, pbrenvmap::Generator::Config {
			.cubemap = { .usage = vk::ImageUsageFlagBits::eSampled },
			.sphericalHarmonicCoefficients = { .usage = vk::BufferUsageFlagBits::eUniformBuffer },
			.prefilteredmap = { .usage = vk::ImageUsageFlagBits::eSampled },
		} };

		const pipelines::BrdfmapComputer brdfmapComputer { gpu.device };

		const vk::raii::DescriptorPool descriptorPool {
			gpu.device,
			vku::PoolSizes { brdfmapComputer.descriptorSetLayouts }.getDescriptorPoolCreateInfo()
		};

		const pipelines::BrdfmapComputer::DescriptorSets brdfmapSets { *gpu.device, *descriptorPool, brdfmapComputer.descriptorSetLayouts };
		gpu.device.updateDescriptorSets(brdfmapSets.getDescriptorWrites0(*brdfmapImageView).get(), {});

		const auto computeCommandPool = createCommandPool(gpu.queueFamilies.compute);
		vku::executeSingleCommand(*gpu.device, *computeCommandPool, gpu.queues.compute, [&](vk::CommandBuffer cb) {
			pbrenvmapGenerator.recordCommands(cb, pbrenvmapPipelines, *eqmapImageView);

			// Change brdfmapImage layout to GENERAL.
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
				{}, {}, {},
				vk::ImageMemoryBarrier {
					{}, vk::AccessFlagBits::eShaderWrite,
					{}, vk::ImageLayout::eGeneral,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					brdfmapImage, vku::fullSubresourceRange(),
				});

			// Compute BRDF.
			brdfmapComputer.compute(cb, brdfmapSets, vku::toExtent2D(brdfmapImage.extent));

			// Image layout transitions \w optional queue family ownership transfer.
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eBottomOfPipe,
				{}, {}, {},
				{
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eShaderWrite, {},
						vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
						gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
						pbrenvmapGenerator.cubemapImage, vku::fullSubresourceRange(),
					},
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eShaderWrite, {},
						vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
						gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
						pbrenvmapGenerator.prefilteredmapImage, vku::fullSubresourceRange(),
					},
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eShaderWrite, {},
						vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
						gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
						brdfmapImage, vku::fullSubresourceRange(),
					},
				});
		});
		gpu.queues.compute.waitIdle();

		vk::raii::ImageView cubemapImageView { gpu.device, pbrenvmapGenerator.cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
		vk::raii::ImageView prefilteredmapImageView { gpu.device, pbrenvmapGenerator.prefilteredmapImage.getViewCreateInfo(vk::ImageViewType::eCube) };

		imageBasedLightingResources.emplace(
		    std::move(pbrenvmapGenerator.cubemapImage),
		    std::move(cubemapImageView),
		    std::move(pbrenvmapGenerator.sphericalHarmonicCoefficientsBuffer),
		    std::move(pbrenvmapGenerator.prefilteredmapImage),
		    std::move(prefilteredmapImageView));
	}

	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		// Acquire resource queue family ownerships.
		if (gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
			std::vector<vk::Buffer> targetBuffers { std::from_range, assetResources.attributeBuffers };
			if (assetResources.materialBuffer) targetBuffers.emplace_back(*assetResources.materialBuffer);
			targetBuffers.append_range(assetResources.indexBuffers | std::views::values);
            for (const auto &[bufferPtrsBuffer, byteStridesBuffer] : assetResources.indexedAttributeMappingBuffers | std::views::values) {
                targetBuffers.emplace_back(bufferPtrsBuffer);
                targetBuffers.emplace_back(byteStridesBuffer);
            }
			if (assetResources.tangentBuffer) targetBuffers.emplace_back(*assetResources.tangentBuffer);

			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
				{}, {},
				targetBuffers
					| std::views::transform([&](vk::Buffer buffer) {
						return vk::BufferMemoryBarrier {
							{}, {},
							gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
							buffer,
							0, vk::WholeSize,
						};
					})
					| std::ranges::to<std::vector>(),
				assetResources.images
					| std::views::transform([&](vk::Image image) {
						return vk::ImageMemoryBarrier {
							{}, vk::AccessFlagBits::eTransferRead,
							{}, {},
							gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
							image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
						};
					})
					| std::ranges::to<std::vector>());
		}

		if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
				{}, {}, {},
				{
					vk::ImageMemoryBarrier {
						{}, {},
						vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
						gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
						imageBasedLightingResources.value().cubemapImage, vku::fullSubresourceRange(),
					},
					vk::ImageMemoryBarrier {
						{}, {},
						vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
						gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
						imageBasedLightingResources.value().prefilteredmapImage, vku::fullSubresourceRange(),
					},
					vk::ImageMemoryBarrier {
						{}, {},
						vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
						gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
						brdfmapImage, vku::fullSubresourceRange(),
					},
				});
		}

		recordGltfFallbackImageClearCommands(cb);
		recordImageMipmapGenerationCommands(cb);
		recordInitialImageLayoutTransitionCommands(cb);
	});
	gpu.queues.graphicsPresent.waitIdle();
}

auto vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(
	vk::SurfaceKHR surface,
	const vk::Extent2D &newExtent
) -> void {
	swapchain = createSwapchain(surface, newExtent, *swapchain);
	swapchainExtent = newExtent;
	swapchainImages = swapchain.getImages();

	swapchainAttachmentGroups = createSwapchainAttachmentGroups();
	imGuiSwapchainAttachmentGroups = createImGuiSwapchainAttachmentGroups();

	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [this](vk::CommandBuffer cb) {
		recordInitialImageLayoutTransitionCommands(cb);
	});
	gpu.queues.graphicsPresent.waitIdle();
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchain(
	vk::SurfaceKHR surface,
	const vk::Extent2D &extent,
	vk::SwapchainKHR oldSwapchain
) const -> decltype(swapchain) {
	const vk::SurfaceCapabilitiesKHR surfaceCapabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);
	return { gpu.device, vk::StructureChain {
		vk::SwapchainCreateInfoKHR{
			vk::SwapchainCreateFlagBitsKHR::eMutableFormat,
			surface,
			std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount),
			vk::Format::eB8G8R8A8Srgb,
			vk::ColorSpaceKHR::eSrgbNonlinear,
			extent,
			1,
			vk::ImageUsageFlagBits::eColorAttachment,
			{}, {},
			surfaceCapabilities.currentTransform,
			vk::CompositeAlphaFlagBitsKHR::eOpaque,
			vk::PresentModeKHR::eFifo,
			true,
			oldSwapchain,
		},
		vk::ImageFormatListCreateInfo {
			vku::unsafeProxy({
				vk::Format::eB8G8R8A8Srgb,
				vk::Format::eB8G8R8A8Unorm,
			}),
		},
	}.get() };
}

auto vk_gltf_viewer::vulkan::SharedData::createGltfFallbackImage() const -> decltype(gltfFallbackImage) {
	return { gpu.allocator, vk::ImageCreateInfo {
        {},
		vk::ImageType::e2D,
		vk::Format::eR8G8B8A8Unorm,
		vk::Extent3D { 1, 1, 1 },
		1, 1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::createBrdfmapImage() const -> decltype(brdfmapImage) {
	return { gpu.allocator, vk::ImageCreateInfo {
        {},
		vk::ImageType::e2D,
		vk::Format::eR16G16Unorm,
		vk::Extent3D { 512, 512, 1 },
		1, 1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchainAttachmentGroups() const -> decltype(swapchainAttachmentGroups) {
	return { std::from_range, swapchainImages | std::views::transform([&](vk::Image image) {
		return SwapchainAttachmentGroup { gpu.device, image, swapchainExtent };
	}) };
}

auto vk_gltf_viewer::vulkan::SharedData::createImGuiSwapchainAttachmentGroups() const -> decltype(imGuiSwapchainAttachmentGroups) {
	return { std::from_range, swapchainImages | std::views::transform([&](vk::Image image) {
		return ImGuiSwapchainAttachmentGroup { gpu.device, image, swapchainExtent };
	}) };
}

auto vk_gltf_viewer::vulkan::SharedData::createCommandPool(
	std::uint32_t queueFamilyIndex
) const -> vk::raii::CommandPool {
	return { gpu.device, vk::CommandPoolCreateInfo{
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyIndex,
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::recordGltfFallbackImageClearCommands(
    vk::CommandBuffer graphicsCommandBuffer
) const -> void {
	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			{}, vk::AccessFlagBits::eTransferWrite,
			{}, vk::ImageLayout::eTransferDstOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			gltfFallbackImage,
			vku::fullSubresourceRange(),
		});
	graphicsCommandBuffer.clearColorImage(
		gltfFallbackImage, vk::ImageLayout::eTransferDstOptimal,
		vk::ClearColorValue { 1.f, 1.f, 1.f, 1.f },
		vku::fullSubresourceRange());
	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			vk::AccessFlagBits::eTransferWrite, {},
			vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			gltfFallbackImage,
			vku::fullSubresourceRange(),
		});
}

auto vk_gltf_viewer::vulkan::SharedData::recordImageMipmapGenerationCommands(
    vk::CommandBuffer graphicsCommandBuffer
) const -> void {
	if (assetResources.images.empty()) return;

	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, {}, {},
		assetResources.images
			| std::views::transform([](vk::Image image) {
				return std::array {
					vk::ImageMemoryBarrier {
						{}, vk::AccessFlagBits::eTransferRead,
						vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
						vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
						image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
					},
					vk::ImageMemoryBarrier {
						{}, vk::AccessFlagBits::eTransferWrite,
						{}, vk::ImageLayout::eTransferDstOptimal,
						vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
						image, { vk::ImageAspectFlagBits::eColor, 1, vk::RemainingArrayLayers, 0, 1 },
					},
				};
			})
			| std::views::join
			| std::ranges::to<std::vector>());

	recordBatchedMipmapGenerationCommand(graphicsCommandBuffer, assetResources.images);

	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		assetResources.images
			| std::views::transform([](const vku::Image &image) {
				return std::array {
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eTransferRead, {},
						vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
						vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
						image, { vk::ImageAspectFlagBits::eColor, 0, image.mipLevels - 1U, 0, 1 },
					},
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eTransferWrite, {},
						vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
						vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
						image, { vk::ImageAspectFlagBits::eColor, image.mipLevels - 1U, 1, 0, 1 },
					},
				};
			})
			| std::views::join
			| std::ranges::to<std::vector>());
}

auto vk_gltf_viewer::vulkan::SharedData::recordInitialImageLayoutTransitionCommands(
	vk::CommandBuffer graphicsCommandBuffer
) const -> void {
	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		swapchainImages
			| std::views::transform([](vk::Image image) {
				return vk::ImageMemoryBarrier{
					{}, {},
					{}, vk::ImageLayout::ePresentSrcKHR,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					image,
					vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
				};
			})
			| std::ranges::to<std::vector>());
}