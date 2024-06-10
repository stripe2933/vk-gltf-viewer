module;

#include <cstdlib>
#include <algorithm>
#include <charconv>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <fastgltf/core.hpp>
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :helpers.ranges;
import :vulkan.frame.SharedData;

vk_gltf_viewer::vulkan::SharedData::SharedData(
    const fastgltf::Asset &asset,
    const std::filesystem::path &assetDir,
    const Gpu &gpu,
    vk::SurfaceKHR surface,
	const vk::Extent2D &swapchainExtent,
    const shaderc::Compiler &compiler
) : asset { asset },
	assetResources { asset, assetDir, gpu },
	sceneResources { asset, asset.scenes[asset.defaultScene.value_or(0)], gpu },
	swapchain { createSwapchain(gpu, surface, swapchainExtent) },
	swapchainExtent { swapchainExtent },
	meshRenderer { gpu.device, static_cast<std::uint32_t>(assetResources.textures.size()), compiler },
	skyboxRenderer { gpu, compiler },
	swapchainAttachmentGroups { createSwapchainAttachmentGroups(gpu.device) },
	graphicsCommandPool { createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent) },
	transferCommandPool { createCommandPool(gpu.device, gpu.queueFamilies.transfer) },
	deviceInfo { *gpu.physicalDevice, *gpu.device, gpu.queues.transfer, *transferCommandPool },
    cubemapTexture { std::getenv("CUBEMAP_PATH"), deviceInfo },
    prefilteredmapTexture { std::getenv("PREFILTEREDMAP_PATH"), deviceInfo },
	cubemapImageView { gpu.device, cubemapTexture.getImageViewCreateInfo() },
	prefilteredmapImageView { gpu.device, prefilteredmapTexture.getImageViewCreateInfo() },
    cubemapSphericalHarmonicsBuffer {
    	gpu.allocator,
    	std::from_range,
    	std::string { std::getenv("CUBEMAP_SH_COEFFS") }
    		| std::views::split(',')
    		| std::views::transform([](auto r) { return std::stof(std::string { r.begin(), r.end() }); })
    		| std::views::take(27)
    		| std::ranges::to<std::vector<float>>(),
    	vk::BufferUsageFlagBits::eUniformBuffer,
    },
	brdfmapImage { gpu.allocator, vk::ImageCreateInfo {
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
	} },
	brdfmapImageView { gpu.device, vk::ImageViewCreateInfo {
        {},
		brdfmapImage,
		vk::ImageViewType::e2D,
		brdfmapImage.format,
		{},
		vku::fullSubresourceRange(),
	} } {
	vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
		if (gpu.queueFamilies.transfer == gpu.queueFamilies.graphicsPresent) return;

		const std::vector<vk::Image> targetIamges { cubemapTexture.image, prefilteredmapTexture.image };
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, {}, {},
			targetIamges
				| std::views::transform([&](vk::Image image) {
					return vk::ImageMemoryBarrier {
						{}, {},
						{}, {},
						gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
						image, vku::fullSubresourceRange(),
					};
				})
				| std::ranges::to<std::vector<vk::ImageMemoryBarrier>>());
	});
	gpu.queues.transfer.waitIdle();

	{
		const pipelines::BrdfmapComputer brdfmapComputer { gpu.device, compiler };

		constexpr std::array poolSizes {
			vk::DescriptorPoolSize { vk::DescriptorType::eStorageImage, 1 },
		};
		const vk::raii::DescriptorPool descriptorPool { gpu.device, vk::DescriptorPoolCreateInfo {
			{},
			1,
			poolSizes,
		} };

		const pipelines::BrdfmapComputer::DescriptorSets brdfmapSets { *gpu.device, *descriptorPool, brdfmapComputer.descriptorSetLayouts };
		gpu.device.updateDescriptorSets(brdfmapSets.getDescriptorWrites0(*brdfmapImageView).get(), {});

		const vk::raii::CommandPool computeCommandPool = createCommandPool(gpu.device, gpu.queueFamilies.compute);
		vku::executeSingleCommand(*gpu.device, *computeCommandPool, gpu.queues.compute, [&](vk::CommandBuffer cb) {
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

			// Change brdfmapImage layout to SHADER_READ_ONLY_OPTIMAL, with optional queue family ownership transfer.
			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eBottomOfPipe,
				{}, {}, {},
				vk::ImageMemoryBarrier {
					vk::AccessFlagBits::eShaderWrite, {},
					vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
					gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
					brdfmapImage, vku::fullSubresourceRange(),
				});
		});
		gpu.queues.compute.waitIdle();
	}

	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		// Acquire resource queue family ownerships.
		if (gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
			std::vector<vk::Buffer> targetBuffers { std::from_range, assetResources.attributeBuffers };
			targetBuffers.emplace_back(assetResources.materialBuffer);
			targetBuffers.append_range(assetResources.indexBuffers | std::views::values);
			if (assetResources.texcoordReferenceBuffer) targetBuffers.emplace_back(*assetResources.texcoordReferenceBuffer);
			if (assetResources.colorReferenceBuffer) targetBuffers.emplace_back(*assetResources.colorReferenceBuffer);
			if (assetResources.texcoordFloatStrideBuffer) targetBuffers.emplace_back(*assetResources.texcoordFloatStrideBuffer);
			if (assetResources.colorFloatStrideBuffer) targetBuffers.emplace_back(*assetResources.colorFloatStrideBuffer);

			std::vector<vk::Image> targetImages { std::from_range, assetResources.images };
			targetImages.emplace_back(cubemapTexture.image);
			targetImages.emplace_back(prefilteredmapTexture.image);

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
				targetImages
					| std::views::transform([&](vk::Image image) {
						return vk::ImageMemoryBarrier {
							{}, {},
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
				vk::ImageMemoryBarrier {
					{}, {},
					vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
					gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
					brdfmapImage, vku::fullSubresourceRange(),
				});
		}

		generateAssetResourceMipmaps(cb);
		initAttachmentLayouts(cb);
	});
	gpu.queues.graphicsPresent.waitIdle();
}

auto vk_gltf_viewer::vulkan::SharedData::handleSwapchainResize(
	const Gpu &gpu,
	vk::SurfaceKHR surface,
	const vk::Extent2D &newExtent
) -> void {
	swapchain = createSwapchain(gpu, surface, newExtent, *swapchain);
	swapchainExtent = newExtent;
	swapchainImages = swapchain.getImages();

	swapchainAttachmentGroups = createSwapchainAttachmentGroups(gpu.device);

	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [this](vk::CommandBuffer cb) {
		initAttachmentLayouts(cb);
	});
	gpu.queues.graphicsPresent.waitIdle();
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchain(
	const Gpu &gpu,
	vk::SurfaceKHR surface,
	const vk::Extent2D &extent,
	vk::SwapchainKHR oldSwapchain
) const -> decltype(swapchain) {
	const vk::SurfaceCapabilitiesKHR surfaceCapabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);
	return { gpu.device, vk::SwapchainCreateInfoKHR{
		{},
		surface,
		std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount),
		vk::Format::eB8G8R8A8Srgb,
		vk::ColorSpaceKHR::eSrgbNonlinear,
		extent,
		1,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
		{}, {},
		surfaceCapabilities.currentTransform,
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		vk::PresentModeKHR::eFifo,
		true,
		oldSwapchain,
	} };
}

auto vk_gltf_viewer::vulkan::SharedData::createSwapchainAttachmentGroups(
	const vk::raii::Device &device
) const -> decltype(swapchainAttachmentGroups) {
	return swapchainImages
		| std::views::transform([&](vk::Image image) {
			vku::AttachmentGroup attachmentGroup { swapchainExtent };
			attachmentGroup.addColorAttachment(
				device,
				{ image, vk::Extent3D { swapchainExtent, 1 }, vk::Format::eB8G8R8A8Srgb, 1, 1 });
			return attachmentGroup;
		})
		| std::ranges::to<std::vector<vku::AttachmentGroup>>();
}

auto vk_gltf_viewer::vulkan::SharedData::createCommandPool(
	const vk::raii::Device &device,
	std::uint32_t queueFamilyIndex
) const -> vk::raii::CommandPool {
	return { device, vk::CommandPoolCreateInfo{
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