module;

#include <array>
#include <compare>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <GLFW/glfw3.h>

module vk_gltf_viewer;
import :MainApp;

import vulkan_hpp;
import vk_mem_alloc_hpp;
import :helpers;

vk_gltf_viewer::MainApp::QueueFamilies::QueueFamilies(
	vk::PhysicalDevice physicalDevice,
	vk::SurfaceKHR surface
) {
	for (auto [queueFamilyIndex, properties] : physicalDevice.getQueueFamilyProperties() | ranges::views::enumerate) {
		if ((properties.queueFlags & vk::QueueFlagBits::eGraphics) && physicalDevice.getSurfaceSupportKHR(queueFamilyIndex, surface)) {
			graphicsPresent = queueFamilyIndex;
			return;
		}
	}

	throw std::runtime_error { "Failed to find the required queue families" };
}


vk_gltf_viewer::MainApp::Queues::Queues(
	vk::Device device,
	const QueueFamilies& queueFamilies
) : graphicsPresent{ device.getQueue(queueFamilies.graphicsPresent, 0) } {}

auto vk_gltf_viewer::MainApp::Queues::getDeviceQueueCreateInfos(
	const QueueFamilies& queueFamilies
) -> std::array<vk::DeviceQueueCreateInfo, 1> {
	static constexpr std::array queuePriorities{ 1.0f };
	return std::array {
		vk::DeviceQueueCreateInfo{
			{},
			queueFamilies.graphicsPresent,
			queuePriorities,
		},
	};
}

vk_gltf_viewer::MainApp::MainApp()
	: pWindow { glfwCreateWindow(800, 600, "Vulkan glTF Viewer", nullptr, nullptr) } {
	if (!pWindow) {
		const char* error;
		const int code = glfwGetError(&error);
		throw std::runtime_error{ std::format("Failed to create GLFW window: {} (error code {})", error, code) };
	}
}

vk_gltf_viewer::MainApp::~MainApp() {
	glfwDestroyWindow(pWindow);
}

auto vk_gltf_viewer::MainApp::run() -> void {
	vk::raii::SwapchainKHR swapchain = createSwapchain();
	std::vector swapchainImages = swapchain.getImages();

	// Descriptor/command pools.
	const vk::raii::CommandPool graphicsCommandPool = createCommandPool(queueFamilies.graphicsPresent);

	// Command buffers.
	const vk::CommandBuffer drawCommandBuffer = (*device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
		*graphicsCommandPool,
		vk::CommandBufferLevel::ePrimary,
		1,
	}).front();

	// Synchronization stuffs.
	const vk::raii::Semaphore swapchainImageAcquireSema{ device, vk::SemaphoreCreateInfo{} };
	const vk::raii::Fence inFlightFence{ device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } };

	const auto initImageLayouts = [&]() -> void {
		const vk::CommandBuffer cb = (*device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
			*graphicsCommandPool,
			vk::CommandBufferLevel::ePrimary,
			1,
		}).front();
		cb.begin(vk::CommandBufferBeginInfo{});

		cb.pipelineBarrier(
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

		cb.end();
		queues.graphicsPresent.submit(vk::SubmitInfo{
			{},
			{},
			cb,
		});
		queues.graphicsPresent.waitIdle();
	};

	const auto recreateSwapchain = [&]() -> void {
		// Yield while window is minimized.
		for (int width, height; !glfwWindowShouldClose(pWindow) && (glfwGetFramebufferSize(pWindow, &width, &height), width == 0 || height == 0); std::this_thread::yield());

		swapchain = createSwapchain(*swapchain);
		swapchainImages = swapchain.getImages();

		initImageLayouts();
	};

	initImageLayouts();

	while (!glfwWindowShouldClose(pWindow)) {
		glfwPollEvents();

		constexpr std::uint64_t MAX_TIMEOUT = std::numeric_limits<std::uint64_t>::max();

		// Wait for the previous frame execution to finish.
		if (auto result = device.waitForFences(*inFlightFence, true, MAX_TIMEOUT); result != vk::Result::eSuccess) {
			throw std::runtime_error{ std::format("Failed to wait for in-flight fence: {}", to_string(result)) };
		}
		device.resetFences(*inFlightFence);

		// Acquire the next swapchain image.
		std::uint32_t imageIndex;
		try {
			imageIndex = (*device).acquireNextImageKHR(*swapchain, MAX_TIMEOUT, *swapchainImageAcquireSema).value;
		}
		catch (const vk::OutOfDateKHRError&) {
			device.waitIdle();
			recreateSwapchain();
			continue;
		}

		// Record draw command.
		drawCommandBuffer.begin(vk::CommandBufferBeginInfo{});
		drawCommandBuffer.end();

		// Submit draw command to the graphics queue.
		constexpr vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		queues.graphicsPresent.submit(vk::SubmitInfo{
			*swapchainImageAcquireSema,
			waitStages,
			drawCommandBuffer,
		}, *inFlightFence);

		// Present the image to the swapchain.
		try {
			// The result codes VK_ERROR_OUT_OF_DATE_KHR and VK_SUBOPTIMAL_KHR have the same meaning when
			// returned by vkQueuePresentKHR as they do when returned by vkAcquireNextImageKHR.
			if (queues.graphicsPresent.presentKHR({ {}, *swapchain, imageIndex }) == vk::Result::eSuboptimalKHR) {
				throw vk::OutOfDateKHRError { "Suboptimal swapchain" };
			}
		}
		catch (const vk::OutOfDateKHRError&) {
			device.waitIdle();
			recreateSwapchain();
		}
	}
	device.waitIdle();
}

auto vk_gltf_viewer::MainApp::createInstance() const -> decltype(instance) {
	constexpr vk::ApplicationInfo appInfo{
		"Vulkan glTF Viewer", 0,
		nullptr, 0,
		vk::makeApiVersion(0, 1, 2, 0),
	};

	const std::vector<const char*> layers{
#ifndef NDEBUG
		"VK_LAYER_KHRONOS_validation",
#endif
	};

	std::vector<const char*> extensions{
#if __APPLE__
		vk::KHRPortabilityEnumerationExtensionName,
#endif
	};
	std::uint32_t glfwExtensionCount;
	const char** const glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	extensions.append_range(std::views::counted(glfwExtensions, glfwExtensionCount));

	return { context, vk::InstanceCreateInfo{
#if __APPLE__
		vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#else
		{},
#endif
		&appInfo,
		layers,
		extensions,
	} };
}

auto vk_gltf_viewer::MainApp::createSurface() const -> decltype(surface) {
	VkSurfaceKHR surface;
	if (glfwCreateWindowSurface(*instance, pWindow, nullptr, &surface) != VK_SUCCESS) {
		const char* error;
		const int code = glfwGetError(&error);
		throw std::runtime_error{ std::format("Failed to create window surface: {} (error code {})", error, code) };
	}
	return { instance, surface };
}

auto vk_gltf_viewer::MainApp::selectPhysicalDevice() const -> decltype(physicalDevice) {
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
	return instance.enumeratePhysicalDevices().front();
}

auto vk_gltf_viewer::MainApp::createDevice() const -> decltype(device) {
	const auto queueCreateInfos = Queues::getDeviceQueueCreateInfos(queueFamilies);
	constexpr std::array extensions{
#if __APPLE__
		vk::KHRPortabilitySubsetExtensionName,
#endif
		vk::KHRSwapchainExtensionName,
	};
	return { physicalDevice, vk::DeviceCreateInfo{
		{},
		queueCreateInfos,
		{},
		extensions,
	} };
}

auto vk_gltf_viewer::MainApp::createAllocator() const -> decltype(allocator) {
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
	const vma::VulkanFunctions vulkanFuncs{
		instance.getDispatcher()->vkGetInstanceProcAddr,
		device.getDispatcher()->vkGetDeviceProcAddr,
	};
	return vma::createAllocator(vma::AllocatorCreateInfo{
		{},
		*physicalDevice,
		*device,
		{}, {}, {}, {}, &vulkanFuncs,
		*instance,
		vk::makeApiVersion(0, 1, 2, 0),
	});
}

auto vk_gltf_viewer::MainApp::createSwapchain(
	vk::SwapchainKHR oldSwapchain
) const -> vk::raii::SwapchainKHR {
	// Get window framebuffer size.
	int width, height;
	glfwGetFramebufferSize(pWindow, &width, &height);

	const vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
	return { device, vk::SwapchainCreateInfoKHR{
		{},
		*surface,
		std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount),
		vk::Format::eB8G8R8A8Srgb,
		vk::ColorSpaceKHR::eSrgbNonlinear,
		vk::Extent2D { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) },
		1,
		vk::ImageUsageFlagBits::eColorAttachment,
		{}, {},
		surfaceCapabilities.currentTransform,
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		vk::PresentModeKHR::eFifo,
		true,
		oldSwapchain,
	} };
}

auto vk_gltf_viewer::MainApp::createCommandPool(
	std::uint32_t queueFamilyIndex
) const -> vk::raii::CommandPool {
	return { device, vk::CommandPoolCreateInfo{
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyIndex,
	} };
}