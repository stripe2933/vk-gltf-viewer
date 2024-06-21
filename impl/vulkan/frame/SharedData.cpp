module;

#include <cstdlib>
#include <algorithm>
#include <array>
#include <charconv>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <fastgltf/core.hpp>
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.frame.SharedData;

import pbrenvmap;
import :helpers.ranges;
import :io.logger;
import :io.StbDecoder;
import :vulkan.pipelines.BrdfmapComputer;

vk_gltf_viewer::vulkan::SharedData::SharedData(
    const fastgltf::Asset &asset,
    const std::filesystem::path &assetDir,
    const Gpu &gpu,
    vk::SurfaceKHR surface,
	const vk::Extent2D &swapchainExtent
) : asset { asset },
	gpu { gpu },
	assetResources { asset, assetDir, gpu },
	swapchain { createSwapchain(surface, swapchainExtent) },
	swapchainExtent { swapchainExtent } {
	const auto eqmapImageData = io::StbDecoder<float>::fromFile(std::getenv("EQMAP_PATH"), 4);
	vku::MappedBuffer eqmapImageStagingBuffer { gpu.allocator, std::from_range, eqmapImageData.asSpan(), vk::BufferUsageFlagBits::eTransferSrc };
	const vku::AllocatedImage eqmapImage {
		gpu.allocator,
		vk::ImageCreateInfo {
			{},
			vk::ImageType::e2D,
			vk::Format::eR32G32B32A32Sfloat,
			vk::Extent3D { eqmapImageData.width, eqmapImageData.height, 1 },
			1, 1,
			vk::SampleCountFlagBits::e1,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
		},
		vma::AllocationCreateInfo {
			{},
			vma::MemoryUsage::eAutoPreferDevice
		},
	};

	const auto transferCommandPool = createCommandPool(gpu.queueFamilies.transfer);
	vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
			{}, {}, {},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eTransferWrite,
				{}, vk::ImageLayout::eTransferDstOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				eqmapImage, vku::fullSubresourceRange(),
			});

		cb.copyBufferToImage(
			eqmapImageStagingBuffer,
			eqmapImage, vk::ImageLayout::eTransferDstOptimal,
			vk::BufferImageCopy {
				0, {}, {},
				{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
				{ 0, 0, 0 },
				eqmapImage.extent,
			});

		// Layout transition + releasing queue family ownerships.
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
			{},
			{}, {},
			vk::ImageMemoryBarrier {
				vk::AccessFlagBits::eTransferWrite, {},
				vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
				gpu.queueFamilies.transfer, gpu.queueFamilies.compute,
				eqmapImage, vku::fullSubresourceRange(),
			});
	});
	gpu.queues.transfer.waitIdle();

	{
		// Create image view for eqmapImage.
		const vk::raii::ImageView eqmapImageView { gpu.device, vk::ImageViewCreateInfo {
			{},
			eqmapImage,
			vk::ImageViewType::e2D,
			eqmapImage.format,
			{},
			vku::fullSubresourceRange(),
		} };

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

		const pipelines::BrdfmapComputer brdfmapComputer { gpu.device, compiler };

		const vk::raii::DescriptorPool descriptorPool {
			gpu.device,
			vku::PoolSizes { brdfmapComputer.descriptorSetLayouts }.getDescriptorPoolCreateInfo()
		};

		const pipelines::BrdfmapComputer::DescriptorSets brdfmapSets { *gpu.device, *descriptorPool, brdfmapComputer.descriptorSetLayouts };
		gpu.device.updateDescriptorSets(brdfmapSets.getDescriptorWrites0(*brdfmapImageView).get(), {});

		const auto computeCommandPool = createCommandPool(gpu.queueFamilies.compute);
		vku::executeSingleCommand(*gpu.device, *computeCommandPool, gpu.queues.compute, [&](vk::CommandBuffer cb) {
            // Acquire queue family ownerships.
            if (gpu.queueFamilies.transfer != gpu.queueFamilies.compute) {
	            cb.pipelineBarrier(
            		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
            		{}, {}, {},
            		vk::ImageMemoryBarrier {
            		    {}, vk::AccessFlagBits::eShaderRead,
            			vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            			gpu.queueFamilies.transfer, gpu.queueFamilies.compute,
            			eqmapImage, vku::fullSubresourceRange(),
            		});
            }

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
				std::array {
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

		vk::raii::ImageView cubemapImageView { gpu.device, vk::ImageViewCreateInfo {
			{},
			pbrenvmapGenerator.cubemapImage,
			vk::ImageViewType::eCube,
			pbrenvmapGenerator.cubemapImage.format,
			{},
			vku::fullSubresourceRange(),
		} };
		vk::raii::ImageView prefilteredmapImageView { gpu.device, vk::ImageViewCreateInfo {
			{},
			pbrenvmapGenerator.prefilteredmapImage,
			vk::ImageViewType::eCube,
			pbrenvmapGenerator.prefilteredmapImage.format,
			{},
			vku::fullSubresourceRange(),
		} };

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
					| std::ranges::to<std::vector<vk::BufferMemoryBarrier>>(),
				assetResources.images
					| std::views::transform([&](vk::Image image) {
						return vk::ImageMemoryBarrier {
							{}, vk::AccessFlagBits::eTransferRead,
							{}, {},
							gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
							image, vku::fullSubresourceRange(),
						};
					})
					| std::ranges::to<std::vector<vk::ImageMemoryBarrier>>());
		}

		if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
				{}, {}, {},
				std::array {
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

		generateAssetResourceMipmaps(cb);
		initAttachmentLayouts(cb);
	});
	gpu.queues.graphicsPresent.waitIdle();

	io::logger::debug("SharedData at {} initialized", static_cast<const void*>(this));
}

auto vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(
	vk::SurfaceKHR surface,
	const vk::Extent2D &newExtent
) -> void {
	swapchain = createSwapchain(surface, newExtent, *swapchain);
	swapchainExtent = newExtent;
	swapchainImages = swapchain.getImages();

	swapchainAttachmentGroups = createSwapchainAttachmentGroups(vk::Format::eB8G8R8A8Srgb);
	imGuiSwapchainAttachmentGroups = createSwapchainAttachmentGroups(vk::Format::eB8G8R8A8Unorm);

	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [this](vk::CommandBuffer cb) {
		initAttachmentLayouts(cb);
	});
	gpu.queues.graphicsPresent.waitIdle();

	io::logger::debug("Swapchain resize handling for SharedData finished");
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchain(
	vk::SurfaceKHR surface,
	const vk::Extent2D &extent,
	vk::SwapchainKHR oldSwapchain
) const -> decltype(swapchain) {
	const vk::SurfaceCapabilitiesKHR surfaceCapabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);
	constexpr std::array swapchainImageFormats {
		vk::Format::eB8G8R8A8Srgb,
		vk::Format::eB8G8R8A8Unorm,
	};
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
			swapchainImageFormats,
		},
	}.get() };
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
	}, vma::AllocationCreateInfo {
        {},
		vma::MemoryUsage::eAutoPreferDevice,
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::createBrdfmapImageView() const -> decltype(brdfmapImageView) {
	return { gpu.device, vk::ImageViewCreateInfo {
        {},
		brdfmapImage,
		vk::ImageViewType::e2D,
		brdfmapImage.format,
		{},
		vku::fullSubresourceRange(),
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchainAttachmentGroups(
	vk::Format mutableFormat
) const -> decltype(swapchainAttachmentGroups) {
	return swapchainImages
		| std::views::transform([&](vk::Image image) {
			vku::AttachmentGroup attachmentGroup { swapchainExtent };
			attachmentGroup.addColorAttachment(
				gpu.device,
				{ image, vk::Extent3D { swapchainExtent, 1 }, vk::Format::eB8G8R8A8Srgb, 1, 1 },
				mutableFormat);
			return attachmentGroup;
		})
		| std::ranges::to<std::vector<vku::AttachmentGroup>>();
}

auto vk_gltf_viewer::vulkan::SharedData::createCommandPool(
	std::uint32_t queueFamilyIndex
) const -> vk::raii::CommandPool {
	return { gpu.device, vk::CommandPoolCreateInfo{
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyIndex,
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::generateAssetResourceMipmaps(
    vk::CommandBuffer commandBuffer
) const -> void {
    // 1. Sort image by their mip levels in ascending order.
    std::vector pImages
        = assetResources.images
        | std::views::transform([](const vku::Image &image) { return &image; })
        | std::ranges::to<std::vector<const vku::Image*>>();
    std::ranges::sort(pImages, {}, [](const vku::Image *pImage) { return pImage->mipLevels; });

    // 2. Generate mipmaps for each image, with global image memory barriers.
    const std::uint32_t maxMipLevels = pImages.back()->mipLevels;
	// TODO: use ranges::views::pairwise when it's available (look's like false-positive compiler error for Clang).
    // for (auto [srcLevel, dstLevel] : std::views::iota(0U, maxMipLevels) | ranges::views::pairwise) {
    for (std::uint32_t srcLevel : std::views::iota(0U, maxMipLevels - 1U)) {
        const std::uint32_t dstLevel = srcLevel + 1;

        // Find the images that have the current mip level.
        auto begin = std::ranges::lower_bound(
            pImages, dstLevel + 1U, {}, [](const vku::Image *pImage) { return pImage->mipLevels; });
        const auto targetImages = std::ranges::subrange(begin, pImages.end()) | ranges::views::deref;

        // Make image barriers that transition the subresource at the srcLevel to TRANSFER_SRC_OPTIMAL.
        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {},
            targetImages
                | std::views::transform([baseMipLevel = srcLevel](const vku::Image &image) {
                    return vk::ImageMemoryBarrier {
                        vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
                        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        image,
                        { vk::ImageAspectFlagBits::eColor, baseMipLevel, 1, 0, vk::RemainingArrayLayers },
                    };
                })
                | std::ranges::to<std::vector<vk::ImageMemoryBarrier>>());

        // Blit from srcLevel to dstLevel.
        for (const vku::Image &image : targetImages) {
        	const vk::Extent2D srcMipExtent = image.mipExtent(srcLevel), dstMipExtent = image.mipExtent(dstLevel);
            commandBuffer.blitImage(
                image, vk::ImageLayout::eTransferSrcOptimal,
                image, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageBlit {
                    { vk::ImageAspectFlagBits::eColor, srcLevel, 0, 1 },
                    { vk::Offset3D{}, vk::Offset3D { static_cast<std::int32_t>(srcMipExtent.width), static_cast<std::int32_t>(srcMipExtent.height), 1 } },
					{ vk::ImageAspectFlagBits::eColor, dstLevel, 0, 1 },
					{ vk::Offset3D{}, vk::Offset3D { static_cast<std::int32_t>(dstMipExtent.width), static_cast<std::int32_t>(dstMipExtent.height), 1 } },
                },
                vk::Filter::eLinear);
        }
    }

    // Change image layouts for sampling.
    commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, {}, {},
        assetResources.images
            | std::views::transform([](vk::Image image) {
                return vk::ImageMemoryBarrier {
                    vk::AccessFlagBits::eTransferWrite, {},
                    {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    image,
                    vku::fullSubresourceRange(),
                };
            })
            | std::ranges::to<std::vector<vk::ImageMemoryBarrier>>());
}

auto vk_gltf_viewer::vulkan::SharedData::initAttachmentLayouts(
	vk::CommandBuffer commandBuffer
) const -> void {
	commandBuffer.pipelineBarrier(
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
			| std::ranges::to<std::vector<vk::ImageMemoryBarrier>>());
}