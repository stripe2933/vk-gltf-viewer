module;

#include <compare>
#include <ranges>
#include <string_view>
#include <thread>
#include <vector>

#include <GLFW/glfw3.h>

module vk_gltf_viewer;
import :MainApp;

import vku;
import :vulkan.frame;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { (Is, __VA_ARGS__)... }; })

auto vk_gltf_viewer::MainApp::run() -> void {
	glm::u32vec2 framebufferSize = window.getFramebufferSize();

	auto sharedData = std::make_shared<vulkan::SharedData>(gpu, *window.surface, vk::Extent2D { framebufferSize.x, framebufferSize.y });
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
	std::array frames = ARRAY_OF(2, vulkan::Frame { gpu, sharedData });
#pragma clang diagnostic pop

	for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); frameIndex = (frameIndex + 1) % frames.size()) {
		glfwPollEvents();
		if (vulkan::Frame &frame = frames[frameIndex]; !frame.onLoop(gpu)) {
			gpu.device.waitIdle();

			// Yield while window is minimized.
			while (!glfwWindowShouldClose(window) && (framebufferSize = window.getFramebufferSize()) == glm::u32vec2 { 0, 0 }) {
				std::this_thread::yield();
			}

			sharedData->handleSwapchainResize(gpu, *window.surface, { framebufferSize.x, framebufferSize.y });
			frame.handleSwapchainResize(gpu, *window.surface, { framebufferSize.x, framebufferSize.y });
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
	extensions.append_range(std::views::counted(glfwGetRequiredInstanceExtensions(&glfwExtensionCount), glfwExtensionCount));

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