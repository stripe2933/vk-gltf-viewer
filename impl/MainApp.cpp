module;

#include <compare>
#include <expected>
#include <format>
#include <ranges>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <vector>

#include <fastgltf/core.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

module vk_gltf_viewer;
import :MainApp;

import vku;
import :io.logger;
import :vulkan.frame.Frame;
import :vulkan.frame.SharedData;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { (Is, __VA_ARGS__)... }; })

vk_gltf_viewer::MainApp::MainApp() {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
}

vk_gltf_viewer::MainApp::~MainApp() {
    ImGui::DestroyContext();
}

auto vk_gltf_viewer::MainApp::run() -> void {
	glm::u32vec2 framebufferSize = window.getFramebufferSize();

	auto sharedData = std::make_shared<vulkan::SharedData>(
		assetExpected.get(), std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path(),
		gpu, *window.surface, vk::Extent2D { framebufferSize.x, framebufferSize.y });
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
	std::array frames = ARRAY_OF(2, vulkan::Frame { appState, sharedData, gpu });
#pragma clang diagnostic pop

	// Init ImGui.
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = { static_cast<float>(framebufferSize.x), static_cast<float>(framebufferSize.y) };
	const glm::vec2 contentScale = window.getContentScale();
	io.DisplayFramebufferScale = { contentScale.x, contentScale.y };
	io.FontGlobalScale = 1.f / io.DisplayFramebufferScale.x;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.Fonts->AddFontFromFileTTF("/Library/Fonts/Arial Unicode.ttf", 16.f * io.DisplayFramebufferScale.x);

	constexpr std::array imGuiDescriptorPoolSizes {
		vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, 1 },
	};
	const vk::raii::DescriptorPool imGuiDescriptorPool { gpu.device, vk::DescriptorPoolCreateInfo {
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		1,
		imGuiDescriptorPoolSizes,
	} };

	ImGui_ImplGlfw_InitForVulkan(window, true);
	ImGui_ImplVulkan_InitInfo initInfo {
		.Instance = *instance,
		.PhysicalDevice = *gpu.physicalDevice,
		.Device = *gpu.device,
		.Queue = gpu.queues.graphicsPresent,
		.DescriptorPool = *imGuiDescriptorPool,
		.MinImageCount = frames.size(),
		.ImageCount = frames.size(),
		.UseDynamicRendering = true,
		.ColorAttachmentFormat = VK_FORMAT_B8G8R8A8_UNORM,
	};
	ImGui_ImplVulkan_Init(&initInfo, nullptr);

	io::logger::debug("Main loop started");

	float elapsedTime = 0.f;
	for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); frameIndex = (frameIndex + 1) % frames.size()) {
        glfwPollEvents();

		const float glfwTime = static_cast<float>(glfwGetTime());
		const float timeDelta = glfwTime - std::exchange(elapsedTime, glfwTime);
		window.update(timeDelta);

        const std::expected onLoopResult = frames[frameIndex].onLoop();
		if (onLoopResult) {
			appState.hoveringNodeIndex = onLoopResult->hoveringNodeIndex;
		}

		if (!onLoopResult || !onLoopResult->presentSuccess) {
			io::logger::debug("Window resizing detected.");
			gpu.device.waitIdle();

			// Yield while window is minimized.
			while (!glfwWindowShouldClose(window) && (framebufferSize = window.getFramebufferSize()) == glm::u32vec2 { 0, 0 }) {
				std::this_thread::yield();
			}

			io::logger::debug("New framebuffer size: ({},{})", framebufferSize.x, framebufferSize.y);
			sharedData->handleSwapchainResize(*window.surface, { framebufferSize.x, framebufferSize.y });
			for (vulkan::Frame &frame : frames) {
				frame.handleSwapchainResize(*window.surface, { framebufferSize.x, framebufferSize.y });
			}

		}
	}
	gpu.device.waitIdle();

	// Shutdown ImGui.
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
}

auto vk_gltf_viewer::MainApp::loadAsset(
    const std::filesystem::path &path
) -> decltype(assetExpected) {
    if (!gltfDataBuffer.loadFromFile(path)) {
        throw std::runtime_error { "Failed to load glTF data buffer" };
    }

    auto asset = fastgltf::Parser{}.loadGltf(&gltfDataBuffer, path.parent_path(), fastgltf::Options::LoadGLBBuffers);
    if (auto error = asset.error(); error != fastgltf::Error::None) {
        throw std::runtime_error { std::format("Failed to load glTF asset: {}", getErrorMessage(error)) };
    }

    return asset;
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
	const auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
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