module;

#include <cstdlib>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <fastgltf/core.hpp>
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.frame.SharedData;

vk_gltf_viewer::vulkan::SharedData::SharedData(
    const Gpu &gpu,
    vk::SurfaceKHR surface,
	const vk::Extent2D &swapchainExtent,
    const shaderc::Compiler &compiler
) : assetResources { assetExpected.get(), std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path(), gpu },
	sceneResources { assetExpected.get(), assetExpected->scenes[assetExpected->defaultScene.value_or(0)], gpu },
	swapchain { createSwapchain(gpu, surface, swapchainExtent) },
	swapchainExtent { swapchainExtent },
	meshRenderer { gpu.device, static_cast<std::uint32_t>(assetResources.textures.size()), compiler },
	swapchainAttachmentGroups { createSwapchainAttachmentGroups(gpu.device) },
	graphicsCommandPool { createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent) } {
	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		acquireResourceQueueFamilyOwnership(gpu.queueFamilies, cb);
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

auto vk_gltf_viewer::vulkan::SharedData::loadGltfDataBuffer(
    const std::filesystem::path &path
) const -> decltype(gltfDataBufferExpected) {
    auto dataBuffer = fastgltf::GltfDataBuffer::FromPath(path);
    if (auto error = dataBuffer.error(); error != fastgltf::Error::None) {
        throw std::runtime_error { std::format("Failed to load glTF data buffer: {}", getErrorMessage(error)) };
    }

    return dataBuffer;
}

auto vk_gltf_viewer::vulkan::SharedData::loadAsset(
    const std::filesystem::path &parentPath
) -> decltype(assetExpected) {
    auto asset = fastgltf::Parser{}.loadGltf(gltfDataBufferExpected.get(), parentPath);
    if (auto error = asset.error(); error != fastgltf::Error::None) {
        throw std::runtime_error { std::format("Failed to load glTF asset: {}", getErrorMessage(error)) };
    }

    return asset;
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

auto vk_gltf_viewer::vulkan::SharedData::acquireResourceQueueFamilyOwnership(
	const Gpu::QueueFamilies &queueFamilies,
	vk::CommandBuffer commandBuffer
) const -> void {
	std::vector<vk::Buffer> targetBuffers;
	targetBuffers.emplace_back(assetResources.materialBuffer);
	targetBuffers.append_range(assetResources.indexBuffers | std::views::values);
	targetBuffers.append_range(assetResources.attributeBuffers);

	std::vector<vk::Image> targetImages { std::from_range, assetResources.images };

	commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eAllCommands,
		{}, {},
		targetBuffers
			| std::views::transform([&](vk::Buffer buffer) {
				return vk::BufferMemoryBarrier {
					{}, {},
					queueFamilies.transfer, queueFamilies.graphicsPresent,
					buffer,
					0, vk::WholeSize,
				};
			})
			| std::ranges::to<std::vector<vk::BufferMemoryBarrier>>(),
		targetImages
			| std::views::transform([&](vk::Image image) {
				return vk::ImageMemoryBarrier {
					{}, {},
					vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
					queueFamilies.transfer, queueFamilies.graphicsPresent,
					image, vku::fullSubresourceRange(),
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