module;

#include <compare>
#include <array>
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
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :MainApp;

import vku;
import :control.ImGui;
import :io.StbDecoder;
import :vulkan.frame.Frame;
import :vulkan.frame.SharedData;

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
	auto graphicsCommandPool = createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent);
	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
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
	ImGui_ImplVulkan_InitInfo initInfo {
		.Instance = *instance,
		.PhysicalDevice = *gpu.physicalDevice,
		.Device = *gpu.device,
		.Queue = gpu.queues.graphicsPresent,
		.DescriptorPool = *imGuiDescriptorPool,
		.MinImageCount = static_cast<std::uint32_t>(frames.size()),
		.ImageCount = static_cast<std::uint32_t>(frames.size()),
		.UseDynamicRendering = true,
		.ColorAttachmentFormat = VK_FORMAT_B8G8R8A8_UNORM,
	};
	ImGui_ImplVulkan_Init(&initInfo, nullptr);

	eqmapImageImGuiDescriptorSet = ImGui_ImplVulkan_AddTexture(*eqmapSampler, *eqmapImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

vk_gltf_viewer::MainApp::~MainApp() {
	ImGui_ImplVulkan_RemoveTexture(eqmapImageImGuiDescriptorSet);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

auto vk_gltf_viewer::MainApp::run() -> void {
	float elapsedTime = 0.f;
	for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); frameIndex = (frameIndex + 1) % frames.size()) {
		const float glfwTime = static_cast<float>(glfwGetTime());
		const float timeDelta = glfwTime - std::exchange(elapsedTime, glfwTime);
		const auto task = update(timeDelta);

        const std::expected frameOnLoopResult = frames[frameIndex].onLoop(task);
		if (frameOnLoopResult) handleOnLoopResult(*frameOnLoopResult);

		if (!frameOnLoopResult || !frameOnLoopResult->presentSuccess) {
			gpu.device.waitIdle();

			// Yield while window is minimized.
			glm::u32vec2 framebufferSize;
			while (!glfwWindowShouldClose(window) && (framebufferSize = window.getFramebufferSize()) == glm::u32vec2 { 0, 0 }) {
				std::this_thread::yield();
			}

			frameSharedData->handleSwapchainResize(*window.surface, { framebufferSize.x, framebufferSize.y });
			for (vulkan::Frame &frame : frames) {
				frame.handleSwapchainResize(*window.surface, { framebufferSize.x, framebufferSize.y });
			}

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

auto vk_gltf_viewer::MainApp::createEqmapImage() -> decltype(eqmapImage) {
	const auto eqmapImageData = io::StbDecoder<float>::fromFile(std::getenv("EQMAP_PATH"), 4);
	const vku::Buffer &eqmapImageStagingBuffer = stagingBuffers.emplace_back(
		gpu.allocator,
		std::from_range, eqmapImageData.asSpan(),
		vk::BufferUsageFlagBits::eTransferSrc);

	const std::array concurrentQueueFamilyIndices {
		gpu.queueFamilies.transfer,
		gpu.queueFamilies.compute,
		gpu.queueFamilies.graphicsPresent,
	};
	vku::AllocatedImage eqmapImage {
		gpu.allocator,
		vk::ImageCreateInfo {
			{},
			vk::ImageType::e2D,
			vk::Format::eR32G32B32A32Sfloat,
			vk::Extent3D { eqmapImageData.width, eqmapImageData.height, 1 },
			vku::Image::maxMipLevels({ eqmapImageData.width, eqmapImageData.height }), 1,
			vk::SampleCountFlagBits::e1,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferDst
				| vk::ImageUsageFlagBits::eSampled /* cubemap generation */
				| vk::ImageUsageFlagBits::eTransferSrc /* mipmap generation */,
			vk::SharingMode::eConcurrent, concurrentQueueFamilyIndices,
		},
		vma::AllocationCreateInfo {
			{},
			vma::MemoryUsage::eAutoPreferDevice
		},
	};

	// TODO: instead creating a temporary command pool, accept the transfer command buffer as a function parameter,
	//  record all required comamnds, and submit (+ wait) once.
	const auto transferCommandPool = createCommandPool(gpu.device, gpu.queueFamilies.transfer);

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
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
			{},
			{}, {},
			vk::ImageMemoryBarrier {
				vk::AccessFlagBits::eTransferWrite, {},
				vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				eqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
			});
	});
	gpu.queues.transfer.waitIdle();
	stagingBuffers.clear();

	return eqmapImage;
}

auto vk_gltf_viewer::MainApp::createEqmapImageView() const -> decltype(eqmapImageView) {
	return { gpu.device, vk::ImageViewCreateInfo {
		{},
		eqmapImage,
		vk::ImageViewType::e2D,
		eqmapImage.format,
		{},
		vku::fullSubresourceRange(),
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
	constexpr std::array imGuiDescriptorPoolSizes {
		vk::DescriptorPoolSize {
			vk::DescriptorType::eCombinedImageSampler,
			1 /* Default ImGui rendering */
			+ 1 /* equirectangular texture */
		},
	};
	return { gpu.device, vk::DescriptorPoolCreateInfo {
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		1 /* Default ImGui rendering */
		+ 1 /* equirectangular texture */,
		imGuiDescriptorPoolSizes,
	} };
}

auto vk_gltf_viewer::MainApp::createFrameSharedData() -> decltype(frameSharedData) {
	const glm::u32vec2 framebufferSize = window.getFramebufferSize();
	return std::make_shared<vulkan::SharedData>(
		assetExpected.get(), std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path(),
		gpu, *window.surface, vk::Extent2D { framebufferSize.x, framebufferSize.y },
		eqmapImage);
}

auto vk_gltf_viewer::MainApp::update(
    float timeDelta
) -> vulkan::Frame::OnLoopTask {
	window.handleEvents(timeDelta);

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// Enable global docking.
	const ImGuiID dockSpaceId = ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);

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
		appState.camera.projection = glm::gtc::perspective(
			appState.camera.getFov(),
			vku::aspect(passthruRect.extent),
			appState.camera.getNear(), appState.camera.getFar());
	}

	control::imgui::hdriEnvironments(eqmapImageImGuiDescriptorSet, { eqmapImage.extent.width, eqmapImage.extent.height }, appState);
	control::imgui::assetSceneHierarchies(assetExpected.get(), appState);
	control::imgui::nodeInspector(assetExpected.get(), appState);

	ImGuizmo::BeginFrame();

	// Capture mouse when using ViewManipulate.
	const bool isUsingImGuizmoViewManipulate = control::imgui::viewManipulate(appState, centerNodeRect.Max);
	glfwSetInputMode(window, GLFW_CURSOR, isUsingImGuizmoViewManipulate ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

	ImGui::Render();

	return {
		.passthruRect = passthruRect,
		.camera = { appState.camera.view, appState.camera.projection },
		.mouseCursorOffset = [&]() -> std::optional<vk::Offset2D> {
			const glm::vec2 framebufferCursorPosition
				= glm::vec2 { window.getCursorPos() } * glm::vec2 { window.getFramebufferSize() } / glm::vec2 { window.getSize() };
			if (glm::vec2 framebufferSize = window.getFramebufferSize();
				framebufferCursorPosition.x >= framebufferSize.x || framebufferCursorPosition.y >= framebufferSize.y) return std::nullopt;
			return vk::Offset2D {
				static_cast<std::int32_t>(framebufferCursorPosition.x),
				static_cast<std::int32_t>(framebufferCursorPosition.y)
			};
		}(),
		.hoveringNodeIndex = appState.hoveringNodeIndex,
		.selectedNodeIndex = appState.selectedNodeIndex,
		.useBlurredSkybox = appState.useBlurredSkybox,
	};
}

auto vk_gltf_viewer::MainApp::handleOnLoopResult(
	const vulkan::Frame::OnLoopResult &onLoopResult
) -> void {
	appState.hoveringNodeIndex = onLoopResult.hoveringNodeIndex;
}

auto vk_gltf_viewer::MainApp::recordImageMipmapGenerationCommands(
	vk::CommandBuffer graphicsCommandBuffer
) const -> void {
	// Change eqmapImage layout.
	// - mipLevel=0: ShaderRead -> TransferSrcOptimal.
	// - mipLevel=1..: Undefined -> TransferDstOptimal.
	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, {}, {},
		std::array {
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eTransferRead,
				vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				eqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
			},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eTransferWrite,
				{}, vk::ImageLayout::eTransferDstOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				eqmapImage, { vk::ImageAspectFlagBits::eColor, 1, vk::RemainingMipLevels, 0, 1 },
			},
		});

	// TODO: use ranges::views::pairwise when it's available (look's like false-positive compiler error for Clang).
	// for (auto [srcLevel, dstLevel] : std::views::iota(0U, eqmapImage.mipLevels) | ranges::views::pairwise) {
	for (std::uint32_t srcLevel : std::views::iota(0U, eqmapImage.mipLevels - 1U)) {
		const std::uint32_t dstLevel = srcLevel + 1;
		// Blit from srcLevel to dstLevel.
		graphicsCommandBuffer.blitImage(
			eqmapImage, vk::ImageLayout::eTransferSrcOptimal,
			eqmapImage, vk::ImageLayout::eTransferDstOptimal,
			vk::ImageBlit {
				{ vk::ImageAspectFlagBits::eColor, srcLevel, 0, 1 },
				{ vk::Offset3D{}, vk::Offset3D { static_cast<std::int32_t>(eqmapImage.extent.width >> srcLevel), static_cast<std::int32_t>(eqmapImage.extent.height >> srcLevel), 1 } },
				{ vk::ImageAspectFlagBits::eColor, dstLevel, 0, 1 },
				{ vk::Offset3D{}, vk::Offset3D { static_cast<std::int32_t>(eqmapImage.extent.width >> dstLevel), static_cast<std::int32_t>(eqmapImage.extent.height >> dstLevel), 1 } },
			},
			vk::Filter::eLinear);

		// Change eqmapImage layout.
		// - mipLevel=srcLevel: TransferSrcOptimal -> ShaderReadOnlyOptimal
		// - mipLevel=dstLevel:
		//   dstLevel is last mip level -> TransferDstOptimal -> ShaderReadOnlyOptimal
		//   otherwise -> TransferDstOptimal -> TransferSrcOptimal
		const std::array imageMemoryBarriers {
			vk::ImageMemoryBarrier2 {
	            vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
				vk::PipelineStageFlagBits2::eAllCommands, {},
				vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				eqmapImage, { vk::ImageAspectFlagBits::eColor, srcLevel, 1, 0, 1 },
			},
			[&, isLastMipLevel = dstLevel == (eqmapImage.mipLevels - 1U)]() {
				return isLastMipLevel
					? vk::ImageMemoryBarrier2 {
						vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
						vk::PipelineStageFlagBits2::eAllCommands, {},
						vk::ImageLayout::eTransferDstOptimal,
						vk::ImageLayout::eShaderReadOnlyOptimal,
						vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
						eqmapImage, { vk::ImageAspectFlagBits::eColor, dstLevel, 1, 0, 1 },
					}
					: vk::ImageMemoryBarrier2 {
			            vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
						vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
						vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
						vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
						eqmapImage, { vk::ImageAspectFlagBits::eColor, dstLevel, 1, 0, 1 },
					};
			}(),
		};
		graphicsCommandBuffer.pipelineBarrier2KHR({
            {},
			{}, {}, imageMemoryBarriers
		});
	}
}