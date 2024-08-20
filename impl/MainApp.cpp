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
import :helpers.functional;
import :helpers.ranges;
import :io.StbDecoder;
import :mipmap;
import :vulkan.Frame;
import :vulkan.generator.ImageBasedLightingResourceGenerator;
import :vulkan.generator.MipmappedCubemapGenerator;
import :vulkan.pipeline.BrdfmapComputer;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })

vk_gltf_viewer::MainApp::MainApp() {
	const vulkan::pipeline::BrdfmapComputer brdfmapComputer { gpu.device };

	const vk::raii::DescriptorPool descriptorPool {
		gpu.device,
		brdfmapComputer.descriptorSetLayout.getPoolSize().getDescriptorPoolCreateInfo(),
	};

	const auto [brdfmapSet] = allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(brdfmapComputer.descriptorSetLayout));
	gpu.device.updateDescriptorSets(
		brdfmapSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *brdfmapImageView, vk::ImageLayout::eGeneral })),
		{});

	const auto [timelineSemaphores, finalWaitValues] = vku::executeHierarchicalCommands(
		gpu.device,
		std::forward_as_tuple(
			// Initialize the image based lighting resources by default(white).
			vku::ExecutionInfo { [this](vk::CommandBuffer cb) {
				initializeImageBasedLightingResourcesByDefault(cb);
			}, *graphicsCommandPool, gpu.queues.graphicsPresent },
			// Create BRDF LUT image.
			vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
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
				brdfmapComputer.compute(cb, brdfmapSet, vku::toExtent2D(brdfmapImage.extent));

				// brdfmapImage will be used as sampled image.
				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eBottomOfPipe,
					{}, {}, {},
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eShaderWrite, {},
						vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
						gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
						brdfmapImage, vku::fullSubresourceRange(),
					});
			}, *computeCommandPool, gpu.queues.compute },
			// Create AssetResource images' mipmaps.
			vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
				if (assetResources.images.empty()) return;

				// Acquire resource queue family ownerships.
				if (gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
					cb.pipelineBarrier(
						vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
						{}, {}, {},
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

				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
					{}, {}, {},
					assetResources.images
						| std::views::transform([](vk::Image image) {
							return vk::ImageMemoryBarrier {
								{}, vk::AccessFlagBits::eTransferRead,
								vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
								vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
								image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
							};
						})
						| std::ranges::to<std::vector>());

				recordBatchedMipmapGenerationCommand(cb, assetResources.images);

				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
					{}, {}, {},
					assetResources.images
						| std::views::transform([](vk::Image image) {
							return vk::ImageMemoryBarrier {
								vk::AccessFlagBits::eTransferWrite, {},
								{}, vk::ImageLayout::eShaderReadOnlyOptimal,
								vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
								image, vku::fullSubresourceRange(),
							};
						})
						| std::ranges::to<std::vector>());
			}, *graphicsCommandPool, gpu.queues.graphicsPresent/*, 2*/ }),
		std::forward_as_tuple(
			// Acquire BRDF LUT image's queue family ownership from compute to graphicsPresent.
			vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
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
			}, *graphicsCommandPool, gpu.queues.graphicsPresent }));

	const vk::Result semaphoreWaitResult = gpu.device.waitSemaphores({
		{},
		vku::unsafeProxy(timelineSemaphores | ranges::views::deref | std::ranges::to<std::vector>()),
		finalWaitValues
	}, ~0U);
	if (semaphoreWaitResult != vk::Result::eSuccess) {
		throw std::runtime_error { "Failed to launch application!" };
	}

	std::tie(imageBasedLightingDescriptorSet) = allocateDescriptorSets(*gpu.device, *(this->descriptorPool), std::tie(
		// TODO: requiring explicit const cast looks bad. vku::allocateDescriptorSets signature should be fixed.
		std::as_const(imageBasedLightingDescriptorSetLayout)));
	gpu.device.updateDescriptorSets({
		imageBasedLightingDescriptorSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorBufferInfo { imageBasedLightingResources.cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize })),
		imageBasedLightingDescriptorSet.getWrite<1>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *imageBasedLightingResources.prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
		imageBasedLightingDescriptorSet.getWrite<2>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
	}, {});

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

	// TODO: due to the ImGui's gamma correction issue, base color/emissive texture is rendered darker than it should be.
	assetTextureDescriptorSets = assetResources.textures
		| std::views::transform([&](const vk::DescriptorImageInfo &textureInfo) -> vk::DescriptorSet {
			return ImGui_ImplVulkan_AddTexture(textureInfo.sampler, textureInfo.imageView, static_cast<VkImageLayout>(textureInfo.imageLayout));
		})
		| std::ranges::to<std::vector>();
}

vk_gltf_viewer::MainApp::~MainApp() {
	for (vk::DescriptorSet textureDescriptorSet : assetTextureDescriptorSets) {
		ImGui_ImplVulkan_RemoveTexture(textureDescriptorSet);
	}
	if (skyboxResources) {
		ImGui_ImplVulkan_RemoveTexture(skyboxResources->imGuiEqmapTextureDescriptorSet);
	}

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

auto vk_gltf_viewer::MainApp::run() -> void {
	const glm::u32vec2 framebufferSize = window.getFramebufferSize();
	vulkan::SharedData sharedData {
		gpu,
		window.getSurface(),
		vk::Extent2D { framebufferSize.x, framebufferSize.y },
		{ assetDescriptorSetLayout, imageBasedLightingDescriptorSetLayout, sceneDescriptorSetLayout, skyboxDescriptorSetLayout }
	};
	std::array frames = ARRAY_OF(2, vulkan::Frame{ gpu, sharedData, assetResources, sceneResources });

	// Optionals that indicates frame should handle swapchain resize to the extent at the corresponding index.
	std::array<std::optional<vk::Extent2D>, std::tuple_size_v<decltype(frames)>> shouldHandleSwapchainResize{};

	const auto [assetDescriptorSet, sceneDescriptorSet]
		= allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(
			// TODO: requiring explicit const cast looks bad. vku::allocateDescriptorSets signature should be fixed.
			std::as_const(assetDescriptorSetLayout),
			std::as_const(sceneDescriptorSetLayout)));

	std::vector<vk::DescriptorImageInfo> assetTextures;
	assetTextures.emplace_back(*sharedData.singleTexelSampler, *sharedData.gltfFallbackImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
	assetTextures.append_range(assetResources.textures);

	gpu.device.updateDescriptorSets({
		assetDescriptorSet.getWrite<0>(assetTextures),
		assetDescriptorSet.getWrite<1>(vku::unsafeProxy(vk::DescriptorBufferInfo { assetResources.materialBuffer, 0, vk::WholeSize })),
		sceneDescriptorSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorBufferInfo { sceneResources.primitiveBuffer, 0, vk::WholeSize })),
		sceneDescriptorSet.getWrite<1>(vku::unsafeProxy(vk::DescriptorBufferInfo { sceneResources.nodeTransformBuffer, 0, vk::WholeSize })),
	}, {});

	float elapsedTime = 0.f;
	for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); frameIndex = (frameIndex + 1) % frames.size()) {
		const float glfwTime = static_cast<float>(glfwGetTime());
		const float timeDelta = glfwTime - std::exchange(elapsedTime, glfwTime);

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
			appState.camera.aspectRatio = vku::aspect(passthruRect.extent);
		}

		// Draw main menu bar.
		visit(multilambda {
			[&](const control::imgui::task::LoadGltf &task) {
				// TODO.
			},
			[&](control::imgui::task::CloseGltf) {
				// TODO.
			},
			[&](const control::imgui::task::LoadEqmap &task) {
				processEqmapChange(task.path);
			},
			[](std::monostate) { },
		}, control::imgui::menuBar());

		control::imgui::inputControlSetting(appState);

		control::imgui::skybox(appState);
		if (skyboxResources) {
			control::imgui::hdriEnvironments(skyboxResources->imGuiEqmapTextureDescriptorSet, appState);
		}

		// Asset inspection.
		fastgltf::Asset &asset = gltfAsset.get();
		const auto assetDir = std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path();
		control::imgui::assetInfos(asset);
		control::imgui::assetBufferViews(asset);
		control::imgui::assetBuffers(asset, assetDir);
		control::imgui::assetImages(asset, assetDir);
		control::imgui::assetSamplers(asset);
		control::imgui::assetMaterials(asset, assetTextureDescriptorSets);
		control::imgui::assetSceneHierarchies(asset, appState);

		// Node inspection.
		control::imgui::nodeInspector(asset, appState);

		ImGuizmo::BeginFrame();

		// Capture mouse when using ViewManipulate.
		control::imgui::viewManipulate(appState, centerNodeRect.Max);

		ImGui::Render();

		vulkan::Frame::ExecutionTask task {
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
			.hoveringNodeOutline = appState.hoveringNodeOutline.to_optional(),
			.selectedNodeOutline = appState.selectedNodeOutline.to_optional(),
			.gltf = vulkan::Frame::ExecutionTask::Gltf {
				.hoveringNodeIndex = appState.hoveringNodeIndex,
				.selectedNodeIndices = appState.selectedNodeIndices,
				.renderingNodeIndices = appState.renderingNodeIndices,
				.assetDescriptorSet = assetDescriptorSet,
				.sceneDescriptorSet = sceneDescriptorSet,
			},
			.imageBasedLightingDescriptorSet = imageBasedLightingDescriptorSet,
			.background = [&]() -> decltype(vulkan::Frame::ExecutionTask::background) {
				if (appState.background.has_value()) {
					return *appState.background;
				}
				else {
					return skyboxResources->descriptorSet;
				}
			}(),
		};
		if (const auto &extent = std::exchange(shouldHandleSwapchainResize[frameIndex], std::nullopt)) {
			task.swapchainResizeHandleInfo.emplace(window.getSurface(), *extent);
		}

        const std::expected frameExecutionResult = frames[frameIndex].execute(task);
		if (frameExecutionResult) {
			// Handle execution result.
			appState.hoveringNodeIndex = frameExecutionResult->hoveringNodeIndex;
		}

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

vk_gltf_viewer::MainApp::GltfAsset::DataBufferLoader::DataBufferLoader(const std::filesystem::path &path) {
	if (!dataBuffer.loadFromFile(path)) {
		throw std::runtime_error { "Failed to load glTF data buffer" };
	}
}

vk_gltf_viewer::MainApp::GltfAsset::GltfAsset(const std::filesystem::path &path)
	: dataBufferLoader { path }
	, assetExpected { fastgltf::Parser{}.loadGltf(&dataBufferLoader.dataBuffer, path.parent_path(), fastgltf::Options::LoadGLBBuffers) } { }

auto vk_gltf_viewer::MainApp::GltfAsset::get() noexcept -> fastgltf::Asset& {
	return assetExpected.get();
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
		{},
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

auto vk_gltf_viewer::MainApp::createDefaultImageBasedLightingResources() const -> ImageBasedLightingResources {
	vku::MappedBuffer sphericalHarmonicsBuffer { gpu.allocator, std::from_range, std::array<glm::vec3, 9> {
		glm::vec3 { 1.f },
	}, vk::BufferUsageFlagBits::eUniformBuffer };
	vku::AllocatedImage prefilteredmapImage { gpu.allocator, vk::ImageCreateInfo {
		vk::ImageCreateFlagBits::eCubeCompatible,
		vk::ImageType::e2D,
		vk::Format::eB10G11R11UfloatPack32,
		vk::Extent3D { 1, 1, 1 },
		1, 6,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
	} };
	vk::raii::ImageView prefilteredmapImageView { gpu.device, prefilteredmapImage.getViewCreateInfo(vk::ImageViewType::eCube) };

	return {
		std::move(sphericalHarmonicsBuffer).unmap(),
		std::move(prefilteredmapImage),
		std::move(prefilteredmapImageView),
	};
}

auto vk_gltf_viewer::MainApp::createEqmapSampler() const -> vk::raii::Sampler {
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

auto vk_gltf_viewer::MainApp::createBrdfmapImage() const -> decltype(brdfmapImage) {
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

auto vk_gltf_viewer::MainApp::createDescriptorPool() const -> decltype(descriptorPool) {
	return {
		gpu.device,
		getPoolSizes(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout, sceneDescriptorSetLayout, skyboxDescriptorSetLayout)
			.getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind),
	};
}

auto vk_gltf_viewer::MainApp::createImGuiDescriptorPool() const -> decltype(imGuiDescriptorPool) {
	return { gpu.device, vk::DescriptorPoolCreateInfo {
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		1 /* Default ImGui rendering */
			+ 1 /* reducedEqmapImage texture */
			+ static_cast<std::uint32_t>(assetResources.textures.size()) /* material textures */,
		vku::unsafeProxy({
			vk::DescriptorPoolSize {
				vk::DescriptorType::eCombinedImageSampler,
				1 /* Default ImGui rendering */
					+ 1 /* reducedEqmapImage texture */
					+ static_cast<std::uint32_t>(assetResources.textures.size()) /* material textures */
			},
		}),
	} };
}

auto vk_gltf_viewer::MainApp::initializeImageBasedLightingResourcesByDefault(
	vk::CommandBuffer graphicsCommandBuffer
) const -> void {
	// Clear prefilteredmapImage to white.
	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			{}, vk::AccessFlagBits::eTransferWrite,
			{}, vk::ImageLayout::eTransferDstOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			imageBasedLightingResources.prefilteredmapImage, vku::fullSubresourceRange(),
		});
	graphicsCommandBuffer.clearColorImage(
		imageBasedLightingResources.prefilteredmapImage, vk::ImageLayout::eTransferDstOptimal,
		vk::ClearColorValue { 1.f, 1.f, 1.f, 0.f },
		vku::fullSubresourceRange());
	graphicsCommandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
		{}, {}, {},
		vk::ImageMemoryBarrier {
			vk::AccessFlagBits::eTransferWrite, {},
			vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
			imageBasedLightingResources.prefilteredmapImage, vku::fullSubresourceRange(),
		});
}

auto vk_gltf_viewer::MainApp::processEqmapChange(
	const std::filesystem::path &eqmapPath
) -> void {
	// Load equirectangular map image and stage it into eqmapImage.
	int width, height;
	if (!stbi_info(eqmapPath.c_str(), &width, &height, nullptr)) {
		throw std::runtime_error { std::format("Failed to load image: {}", stbi_failure_reason()) };
	}

	const vk::Extent2D eqmapImageExtent { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
	std::uint32_t eqmapImageMipLevels = 0;
	for (std::uint32_t mipWidth = eqmapImageExtent.width; mipWidth > 512; mipWidth >>= 1, ++eqmapImageMipLevels);

	const vku::AllocatedImage eqmapImage { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eR32G32B32A32Sfloat,
		vk::Extent3D { eqmapImageExtent, 1 },
		eqmapImageMipLevels, 1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled /* cubemap generation */ | vk::ImageUsageFlagBits::eTransferSrc /* mipmap generation */,
	} };
	std::unique_ptr<vku::AllocatedBuffer> eqmapStagingBuffer;

	const vk::Extent2D reducedEqmapImageExtent = eqmapImage.mipExtent(eqmapImage.mipLevels - 1);
	vku::AllocatedImage reducedEqmapImage { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eB10G11R11UfloatPack32,
		vk::Extent3D { reducedEqmapImageExtent, 1 },
		vku::Image::maxMipLevels(reducedEqmapImageExtent), 1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
	} };

	constexpr vulkan::MipmappedCubemapGenerator::Config mippedCubemapGeneratorConfig {
		.cubemapSize = 1024,
		.cubemapUsage = vk::ImageUsageFlagBits::eSampled,
	};
	vulkan::MipmappedCubemapGenerator mippedCubemapGenerator { gpu, mippedCubemapGeneratorConfig };
	const vulkan::MipmappedCubemapGenerator::Pipelines mippedCubemapGeneratorPipelines {
		vulkan::pipeline::CubemapComputer { gpu.device },
		vulkan::pipeline::SubgroupMipmapComputer { gpu.device, vku::Image::maxMipLevels(mippedCubemapGeneratorConfig.cubemapSize), 32 /*TODO: use proper subgroup size!*/ },
	};

	// Generate IBL resources.
	constexpr vulkan::ImageBasedLightingResourceGenerator::Config iblGeneratorConfig {
		.prefilteredmapImageUsage = vk::ImageUsageFlagBits::eSampled,
		.sphericalHarmonicsBufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
	};
	vulkan::ImageBasedLightingResourceGenerator iblGenerator { gpu, iblGeneratorConfig };
	const vulkan::ImageBasedLightingResourceGenerator::Pipelines iblGeneratorPipelines {
		vulkan::pipeline::PrefilteredmapComputer { gpu.device, { vku::Image::maxMipLevels(iblGeneratorConfig.prefilteredmapSize), 1024 } },
		vulkan::pipeline::SphericalHarmonicsComputer { gpu.device },
		vulkan::pipeline::SphericalHarmonicCoefficientsSumComputer { gpu.device },
		vulkan::pipeline::MultiplyComputer { gpu.device },
	};

	const auto [timelineSemaphores, finalWaitValues] = executeHierarchicalCommands(
		gpu.device,
		std::forward_as_tuple(
			// Create device-local eqmap image from staging buffer.
			vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
				eqmapStagingBuffer = std::make_unique<vku::AllocatedBuffer>(vku::MappedBuffer {
					gpu.allocator,
					std::from_range, io::StbDecoder<float>::fromFile(eqmapPath.c_str(), 4).asSpan(),
					vk::BufferUsageFlagBits::eTransferSrc
				}.unmap());

				// eqmapImage layout transition for copy destination.
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
					*eqmapStagingBuffer,
					eqmapImage, vk::ImageLayout::eTransferDstOptimal,
					vk::BufferImageCopy {
						0, {}, {},
						{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
						{ 0, 0, 0 },
						eqmapImage.extent,
					});

				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
					{}, {}, {},
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eTransferWrite, {},
						vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
						gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
						eqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
					});
			}, *transferCommandPool, gpu.queues.transfer }),
		std::forward_as_tuple(
			// Generate eqmapImage mipmaps and blit its last mip level image to reducedEqmapImage.
			vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
				if (gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
					cb.pipelineBarrier(
						vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
						{}, {}, {},
						vk::ImageMemoryBarrier {
							{}, vk::AccessFlagBits::eTransferRead,
							vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
							gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
							eqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
						});
				}

				// Generate eqmapImage mipmaps.
				recordMipmapGenerationCommand(cb, eqmapImage);

				cb.pipelineBarrier2KHR({
					{}, {}, {},
					vku::unsafeProxy({
						vk::ImageMemoryBarrier2 {
							vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
							vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
							vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
							vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
							eqmapImage, { vk::ImageAspectFlagBits::eColor, eqmapImage.mipLevels - 1, 1, 0, 1 },
						},
						vk::ImageMemoryBarrier2 {
							{}, {},
							vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite,
							{}, vk::ImageLayout::eTransferDstOptimal,
							vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
							reducedEqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
						},
					}),
				});

				// Blit from eqmapImage[level=-1] to reducedEqmapImage[level=0], whose extents are the same.
				cb.blitImage(
					eqmapImage, vk::ImageLayout::eTransferSrcOptimal,
					reducedEqmapImage, vk::ImageLayout::eTransferDstOptimal,
					vk::ImageBlit {
						{ vk::ImageAspectFlagBits::eColor, eqmapImage.mipLevels - 1, 0, 1 },
						{ vk::Offset3D{}, vk::Offset3D { vku::toOffset2D(eqmapImage.mipExtent(eqmapImage.mipLevels - 1)), 1 } },
						{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
						{ vk::Offset3D{}, vk::Offset3D { vku::toOffset2D(vku::toExtent2D(reducedEqmapImage.extent)), 1 } },
					},
					vk::Filter::eLinear);

				// eqmapImage[level=0] will be used as sampled image (other mip levels will not be used).
				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
					{}, {}, {},
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eTransferRead, {},
						vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
						gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
						eqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
					});
			}, *graphicsCommandPool, gpu.queues.graphicsPresent }),
		std::forward_as_tuple(
			// Generate reducedEqmapImage mipmaps.
			vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
					{}, {}, {},
					{
						vk::ImageMemoryBarrier {
							{}, vk::AccessFlagBits::eTransferRead,
							vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
							vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
							reducedEqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
						},
						vk::ImageMemoryBarrier {
							{}, vk::AccessFlagBits::eTransferWrite,
							{}, vk::ImageLayout::eTransferDstOptimal,
							vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
							reducedEqmapImage, { vk::ImageAspectFlagBits::eColor, 1, vk::RemainingMipLevels, 0, 1 },
						},
					});

				// Generate reducedEqmapImage mipmaps.
				recordMipmapGenerationCommand(cb, reducedEqmapImage);

				// reducedEqmapImage will be used as sampled image.
				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
					{}, {}, {},
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eTransferWrite, {},
						{}, vk::ImageLayout::eShaderReadOnlyOptimal,
						vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
						reducedEqmapImage, vku::fullSubresourceRange(),
					});
			}, *graphicsCommandPool, gpu.queues.graphicsPresent/*, 4*/ },
			// Generate cubemap with mipmaps from eqmapImage, and generate IBL resources from the cubemap.
			vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
				if (gpu.queueFamilies.graphicsPresent != gpu.queueFamilies.compute) {
					// Do queue family ownership transfer from graphicsPresent to compute, if required.
					cb.pipelineBarrier(
						vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
						{}, {}, {},
						vk::ImageMemoryBarrier {
							{}, vk::AccessFlagBits::eShaderRead,
							vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
							gpu.queueFamilies.graphicsPresent, gpu.queueFamilies.compute,
							eqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
						});
				}

				mippedCubemapGenerator.recordCommands(cb, mippedCubemapGeneratorPipelines, eqmapImage);

				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eBottomOfPipe,
					{}, {}, {},
					vk::ImageMemoryBarrier {
						vk::AccessFlagBits::eShaderWrite, {},
						vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
						vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
						mippedCubemapGenerator.cubemapImage, vku::fullSubresourceRange(),
					});

				iblGenerator.recordCommands(cb, iblGeneratorPipelines, mippedCubemapGenerator.cubemapImage);

				// Cubemap and prefilteredmap will be used as sampled image.
				cb.pipelineBarrier(
					vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eBottomOfPipe,
					{}, {}, {},
					{
						vk::ImageMemoryBarrier {
							vk::AccessFlagBits::eShaderWrite, {},
							vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
							gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
							mippedCubemapGenerator.cubemapImage, vku::fullSubresourceRange(),
						},
						vk::ImageMemoryBarrier {
							vk::AccessFlagBits::eShaderWrite, {},
							vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
							gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
							iblGenerator.prefilteredmapImage, vku::fullSubresourceRange(),
						},
					});
			}, *computeCommandPool, gpu.queues.compute }),
		std::forward_as_tuple(
			// Acquire resources' queue family ownership from compute to graphicsPresent.
			vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
				if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
					cb.pipelineBarrier(
						vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
						{}, {}, {},
						{
							vk::ImageMemoryBarrier {
								{}, {},
								vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
								gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
								mippedCubemapGenerator.cubemapImage, vku::fullSubresourceRange(),
							},
							vk::ImageMemoryBarrier {
								{}, {},
								vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
								gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
								iblGenerator.prefilteredmapImage, vku::fullSubresourceRange(),
							},
						});
				}
			}, *graphicsCommandPool, gpu.queues.graphicsPresent }));

	const vk::Result semaphoreWaitResult = gpu.device.waitSemaphores({
		{},
		vku::unsafeProxy(timelineSemaphores | ranges::views::deref | std::ranges::to<std::vector>()),
		finalWaitValues
	}, ~0U);
	if (semaphoreWaitResult != vk::Result::eSuccess) {
		throw std::runtime_error { "Failed to launch application!" };
	}

	// Update AppState.
	appState.canSelectSkyboxBackground = true;
	appState.background.reset();
	appState.imageBasedLightingProperties = AppState::ImageBasedLighting {
		.eqmap = {
			.path = eqmapPath,
			.dimension = { eqmapImage.extent.width, eqmapImage.extent.height },
		},
		.cubemap = {
			.size = mippedCubemapGeneratorConfig.cubemapSize,
		},
		.prefilteredmap = {
			.size = iblGeneratorConfig.prefilteredmapSize,
			.roughnessLevels = vku::Image::maxMipLevels(iblGeneratorConfig.prefilteredmapSize),
			.sampleCount = 1024,
		}
	};
	std::ranges::copy(
		iblGenerator.sphericalHarmonicsBuffer.asRange<const glm::vec3>(),
		appState.imageBasedLightingProperties->diffuseIrradiance.sphericalHarmonicCoefficients.begin());

	const vku::DescriptorSet skyboxDescriptorSet = [&]() {
		if (skyboxResources){
			return skyboxResources->descriptorSet;
		}
		else {
			// Allocate new descriptor set.
			return get<0>(allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(
				// TODO: requiring explicit const cast looks bad. vku::allocateDescriptorSets signature should be fixed.
				std::as_const(skyboxDescriptorSetLayout))));
		}
	}();

	if (skyboxResources){
		// Since a descriptor set allocated using ImGui_ImplVulkan_AddTexture cannot be updated, it has to be freed
		// and re-allocated (which done in below).
		(*gpu.device).freeDescriptorSets(*imGuiDescriptorPool, skyboxResources->imGuiEqmapTextureDescriptorSet);
	}

	// Emplace the results into skyboxResources and imageBasedLightingResources.
	vk::raii::ImageView reducedEqmapImageView { gpu.device, reducedEqmapImage.getViewCreateInfo() };
	const vk::DescriptorSet imGuiEqmapImageDescriptorSet
		= ImGui_ImplVulkan_AddTexture(*reducedEqmapSampler, *reducedEqmapImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	vk::raii::ImageView cubemapImageView { gpu.device, mippedCubemapGenerator.cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
	skyboxResources.emplace(
		std::move(reducedEqmapImage),
		std::move(reducedEqmapImageView),
		std::move(mippedCubemapGenerator.cubemapImage),
		std::move(cubemapImageView),
		imGuiEqmapImageDescriptorSet,
		skyboxDescriptorSet);

	vk::raii::ImageView prefilteredmapImageView { gpu.device, iblGenerator.prefilteredmapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
	imageBasedLightingResources = {
		std::move(iblGenerator.sphericalHarmonicsBuffer).unmap(),
		std::move(iblGenerator.prefilteredmapImage),
		std::move(prefilteredmapImageView),
	};

	// Update the related descriptor sets.
	gpu.device.updateDescriptorSets({
		imageBasedLightingDescriptorSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorBufferInfo { imageBasedLightingResources.cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize })),
		imageBasedLightingDescriptorSet.getWrite<1>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *imageBasedLightingResources.prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
		skyboxDescriptorSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *skyboxResources->cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
	}, {});
}
