module;

#include <fastgltf/core.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <stb_image.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :MainApp;

import std;
import vku;
import :control.ImGui;
import :helpers.ranges;
import :mipmap;
import :io.StbDecoder;
import :vulkan.Frame;
import :vulkan.SharedData;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })

[[nodiscard]] auto createCommandPool(
	const vk::raii::Device &device,
	std::uint32_t queueFamilyIndex
) -> vk::raii::CommandPool {
	return { device, vk::CommandPoolCreateInfo{
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyIndex,
	} };
}

vk_gltf_viewer::MainApp::MainApp() {
	const auto transferCommandPool = createCommandPool(gpu.device, gpu.queueFamilies.transfer);
	vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
		recordEqmapStagingCommands(cb);
	});
	gpu.queues.transfer.waitIdle();
	stagingBuffers.clear();

	const auto graphicsCommandPool = createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent);
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

		recordImageMipmapGenerationCommands(cb);
	});
	gpu.queues.graphicsPresent.waitIdle();

	// Init ImGui.
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();
	const glm::vec2 framebufferSize = window.getFramebufferSize();
	io.DisplaySize = { framebufferSize.x, framebufferSize.y };
	const glm::vec2 contentScale = window.getContentScale();
	io.DisplayFramebufferScale = { contentScale.x, contentScale.y };
	io.FontGlobalScale = 1.f / io.DisplayFramebufferScale.x;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.Fonts->AddFontFromFileTTF("/Library/Fonts/Arial Unicode.ttf", 16.f * io.DisplayFramebufferScale.x);

	ImGui_ImplGlfw_InitForVulkan(window, true);
	const auto colorAttachmentFormats = { vk::Format::eB8G8R8A8Unorm };
	ImGui_ImplVulkan_InitInfo initInfo {
		.Instance = *instance,
		.PhysicalDevice = *gpu.physicalDevice,
		.Device = *gpu.device,
		.Queue = gpu.queues.graphicsPresent,
		.DescriptorPool = *imGuiDescriptorPool,
		.MinImageCount = 2,
		.ImageCount = 2,
		.UseDynamicRendering = true,
		.PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfo {
			{},
			colorAttachmentFormats,
		},
	};
	ImGui_ImplVulkan_Init(&initInfo);

	eqmapImageImGuiDescriptorSet = ImGui_ImplVulkan_AddTexture(*eqmapSampler, *eqmapImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

vk_gltf_viewer::MainApp::~MainApp() {
	ImGui_ImplVulkan_RemoveTexture(eqmapImageImGuiDescriptorSet);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

auto vk_gltf_viewer::MainApp::run() -> void {
	const glm::u32vec2 framebufferSize = window.getFramebufferSize();
	vulkan::SharedData sharedData {
		assetExpected.get(),
		gpu, window.getSurface(), vk::Extent2D { framebufferSize.x, framebufferSize.y },
		eqmapImage
	};
	std::array frames = ARRAY_OF(2, vulkan::Frame{ gpu, sharedData, assetResources, sceneResources });

	// Optionals that indicates frame should handle swapchain resize to the extent at the corresponding index.
	std::array<std::optional<vk::Extent2D>, std::tuple_size_v<decltype(frames)>> shouldHandleSwapchainResize{};

	float elapsedTime = 0.f;
	for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); frameIndex = (frameIndex + 1) % frames.size()) {
		const float glfwTime = static_cast<float>(glfwGetTime());
		const float timeDelta = glfwTime - std::exchange(elapsedTime, glfwTime);

		vulkan::Frame::ExecutionTask task = update(timeDelta);
		if (const auto &extent = std::exchange(shouldHandleSwapchainResize[frameIndex], std::nullopt)) {
			task.swapchainResizeHandleInfo.emplace(window.getSurface(), *extent);
		}

        const std::expected frameExecutionResult = frames[frameIndex].execute(task);
		if (frameExecutionResult) handleExecutionResult(*frameExecutionResult);

		if (!frameExecutionResult || !frameExecutionResult->presentSuccess) {
			gpu.device.waitIdle();

			// Yield while window is minimized.
			glm::u32vec2 framebufferSize;
			while (!glfwWindowShouldClose(window) && (framebufferSize = window.getFramebufferSize()) == glm::u32vec2 { 0, 0 }) {
				std::this_thread::yield();
			}

			sharedData.handleSwapchainResize(window.getSurface(), { framebufferSize.x, framebufferSize.y });
			// Frames should handle swapchain resize with extent=framebufferSize.
			shouldHandleSwapchainResize.fill(vk::Extent2D { framebufferSize.x, framebufferSize.y });
		}
	}
	gpu.device.waitIdle();
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
	vk::raii::Instance instance { context, vk::InstanceCreateInfo{
#if __APPLE__
		vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#else
		{},
#endif
		vku::unsafeAddress(vk::ApplicationInfo {
			"Vulkan glTF Viewer", 0,
			nullptr, 0,
			vk::makeApiVersion(0, 1, 2, 0),
		}),
#ifndef NDEBUG
		vku::unsafeProxy<const char* const>({
			"VK_LAYER_KHRONOS_validation",
		}),
#else
		{},
#endif
		vku::unsafeProxy([]() {
			std::vector<const char*> extensions{
#if __APPLE__
				vk::KHRPortabilityEnumerationExtensionName,
#endif
			};

			std::uint32_t glfwExtensionCount;
			const auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
			extensions.append_range(std::views::counted(glfwExtensions, glfwExtensionCount));

			return extensions;
		}()),
	} };
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
	return instance;
}

auto vk_gltf_viewer::MainApp::createEqmapImage() -> decltype(eqmapImage) {
	int width, height;
	if (!stbi_info(std::getenv("EQMAP_PATH"), &width, &height, nullptr)) {
		throw std::runtime_error { std::format("Failed to load image: {}", stbi_failure_reason()) };
	}

	return { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eR32G32B32A32Sfloat,
		vk::Extent3D { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1 },
		vku::Image::maxMipLevels({ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) }), 1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled /* cubemap generation */ | vk::ImageUsageFlagBits::eTransferSrc /* mipmap generation */,
		vk::SharingMode::eConcurrent, vku::unsafeProxy(gpu.queueFamilies.getUniqueIndices()),
	} };
}

auto vk_gltf_viewer::MainApp::createEqmapSampler() const -> decltype(eqmapSampler) {
	return { gpu.device, vk::SamplerCreateInfo {
		{},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		{}, {}, {},
		{},
		{}, {},
		{}, {},
		0, vk::LodClampNone,
	} };
}

auto vk_gltf_viewer::MainApp::createImGuiDescriptorPool() const -> decltype(imGuiDescriptorPool) {
	return { gpu.device, vk::DescriptorPoolCreateInfo {
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		1 /* Default ImGui rendering */
			+ 1 /* equirectangular texture */,
		vku::unsafeProxy({
			vk::DescriptorPoolSize {
				vk::DescriptorType::eCombinedImageSampler,
				1 /* Default ImGui rendering */
					+ 1 /* equirectangular texture */
			},
		}),
	} };
}

auto vk_gltf_viewer::MainApp::recordEqmapStagingCommands(
	vk::CommandBuffer transferCommandBuffer
) -> void {
	const auto eqmapImageData = io::StbDecoder<float>::fromFile(std::getenv("EQMAP_PATH"), 4);
	const vku::Buffer &eqmapImageStagingBuffer = stagingBuffers.emplace_back(
		gpu.allocator,
		std::from_range, eqmapImageData.asSpan(),
		vk::BufferUsageFlagBits::eTransferSrc);

	transferCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			{}, vk::AccessFlagBits::eTransferWrite,
			{}, vk::ImageLayout::eTransferDstOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			eqmapImage, vku::fullSubresourceRange(),
		});
	transferCommandBuffer.copyBufferToImage(
		eqmapImageStagingBuffer,
		eqmapImage, vk::ImageLayout::eTransferDstOptimal,
		vk::BufferImageCopy {
			0, {}, {},
			{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
			{ 0, 0, 0 },
			eqmapImage.extent,
		});
}

auto vk_gltf_viewer::MainApp::recordImageMipmapGenerationCommands(
    vk::CommandBuffer graphicsCommandBuffer
) const -> void {
	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, {}, {},
		[&]() {
			std::vector barriers {
				// Eqmap mipmap generation barriers.
				vk::ImageMemoryBarrier {
					{}, vk::AccessFlagBits::eTransferRead,
					vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					eqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
				},
			};

			// Append AssetResources image barriers.
			barriers.append_range(assetResources.images | std::views::transform([](vk::Image image) {
				return vk::ImageMemoryBarrier {
					{}, vk::AccessFlagBits::eTransferRead,
					vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
				};
			}));

			return barriers;
		}());

	recordBatchedMipmapGenerationCommand(graphicsCommandBuffer, assetResources.images);
	recordMipmapGenerationCommand(graphicsCommandBuffer, eqmapImage);

	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		[&]() {
			std::vector barriers {
				// Eqmap mipmap generation barriers.
				vk::ImageMemoryBarrier {
					vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite, {},
					{}, vk::ImageLayout::eShaderReadOnlyOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					eqmapImage, vku::fullSubresourceRange(),
				},
			};

			// Append AssetResources image barriers.
			barriers.append_range(assetResources.images | std::views::transform([](const vku::Image &image) {
				return vk::ImageMemoryBarrier {
					vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite, {},
					{}, vk::ImageLayout::eShaderReadOnlyOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					image, vku::fullSubresourceRange(),
				};
			}));

			return barriers;
		}());
}

auto vk_gltf_viewer::MainApp::update(
    float timeDelta
) -> vulkan::Frame::ExecutionTask {
	window.handleEvents(timeDelta);

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// Enable global docking.
	const ImGuiID dockSpaceId = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);

	// Get central node region.
	const ImRect centerNodeRect = ImGui::DockBuilderGetCentralNode(dockSpaceId)->Rect();

	// Calculate framebuffer coordinate based passthru rect.
	const ImVec2 imGuiViewportSize = ImGui::GetIO().DisplaySize;
	const glm::vec2 scaleFactor = glm::vec2 { window.getFramebufferSize() } / glm::vec2 { imGuiViewportSize.x, imGuiViewportSize.y };
	const vk::Rect2D passthruRect {
	    { static_cast<std::int32_t>(centerNodeRect.Min.x * scaleFactor.x), static_cast<std::int32_t>(centerNodeRect.Min.y * scaleFactor.y) },
	    { static_cast<std::uint32_t>(centerNodeRect.GetWidth() * scaleFactor.x), static_cast<std::uint32_t>(centerNodeRect.GetHeight() * scaleFactor.y) },
	};

	// Assign the passthruRect to appState.passthruRect. Handle stuffs that are dependent to the it.
	static vk::Rect2D previousPassthruRect{};
	if (vk::Rect2D oldPassthruRect = std::exchange(previousPassthruRect, passthruRect); oldPassthruRect != passthruRect) {
		appState.camera.aspectRatio = static_cast<float>(passthruRect.extent.width) / passthruRect.extent.height;
	}

	control::imgui::inputControlSetting(appState);
	control::imgui::hdriEnvironments(eqmapImageImGuiDescriptorSet, appState);
	control::imgui::assetInspector(assetExpected.get(), std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path(), appState);
	control::imgui::nodeInspector(assetExpected.get(), appState);

	ImGuizmo::BeginFrame();

	// Capture mouse when using ViewManipulate.
	control::imgui::viewManipulate(appState, centerNodeRect.Max);

	ImGui::Render();

	return {
		.passthruRect = passthruRect,
		.camera = { appState.camera.getViewMatrix(), appState.camera.getProjectionMatrix() },
		.mouseCursorOffset = appState.hoveringMousePosition.and_then([&](const glm::vec2 &position) -> std::optional<vk::Offset2D> {
			// If cursor is outside the framebuffer, cursor position is undefined.
			const glm::vec2 framebufferCursorPosition = position * glm::vec2 { window.getFramebufferSize() } / glm::vec2 { window.getSize() };
			if (glm::vec2 framebufferSize = window.getFramebufferSize(); framebufferCursorPosition.x >= framebufferSize.x || framebufferCursorPosition.y >= framebufferSize.y) return std::nullopt;

			return vk::Offset2D {
				static_cast<std::int32_t>(framebufferCursorPosition.x),
				static_cast<std::int32_t>(framebufferCursorPosition.y)
			};
		}),
		.hoveringNodeIndex = appState.hoveringNodeIndex,
		.selectedNodeIndex = appState.selectedNodeIndex,
		.hoveringNodeOutline = appState.hoveringNodeOutline.to_optional(),
		.selectedNodeOutline = appState.selectedNodeOutline.to_optional(),
		.useBlurredSkybox = appState.useBlurredSkybox,
	};
}

auto vk_gltf_viewer::MainApp::handleExecutionResult(
	const vulkan::Frame::ExecutionResult &executionResult
) -> void {
	appState.hoveringNodeIndex = executionResult.hoveringNodeIndex;
}