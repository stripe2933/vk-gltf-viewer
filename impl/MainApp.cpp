module;

#include <compare>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

module vk_gltf_viewer;
import :MainApp;

import vku;
import :vulkan.frame;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { (Is, __VA_ARGS__)... }; })

vk_gltf_viewer::MainApp::MainApp()
	: pWindow { glfwCreateWindow(800, 480, "Vulkan glTF Viewer", nullptr, nullptr) } {
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
	int width, height;
	glfwGetFramebufferSize(pWindow, &width, &height);

	auto sharedData = std::make_shared<vulkan::SharedData>(gpu, *surface, vk::Extent2D { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) });
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
	std::array frames = ARRAY_OF(2, vulkan::Frame { gpu, sharedData });
#pragma clang diagnostic pop

	for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(pWindow); frameIndex = (frameIndex + 1) % frames.size()) {
		glfwPollEvents();
		if (vulkan::Frame &frame = frames[frameIndex]; !frame.onLoop(gpu)) {
			gpu.device.waitIdle();

			// Yield while window is minimized.
			int width, height;
			while (!glfwWindowShouldClose(pWindow) && (glfwGetFramebufferSize(pWindow, &width, &height), width == 0 || height == 0)) {
				std::this_thread::yield();
			}

			const vk::Extent2D newSwapchainExtent { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
			sharedData->handleSwapchainResize(gpu, *surface, newSwapchainExtent);
			frame.handleSwapchainResize(gpu, *surface, newSwapchainExtent);
		}
	}
	gpu.device.waitIdle();
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