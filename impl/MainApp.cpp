module;

#include <fastgltf/core.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <shaderc/shaderc.hpp>
#include <stb_image.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :MainApp;

import std;
import pbrenvmap;
import :control.ImGui;
import :helpers.ranges;
import :io.StbDecoder;
import :mipmap;
import :vulkan.Frame;
import :vulkan.pipeline.BrdfmapComputer;

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
	// Load equirectangular map image and stage it into eqmapImage.
	int width, height;
	if (!stbi_info(std::getenv("EQMAP_PATH"), &width, &height, nullptr)) {
		throw std::runtime_error { std::format("Failed to load image: {}", stbi_failure_reason()) };
	}

	const vk::Extent2D eqmapImageExtent { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
	const vku::AllocatedImage eqmapImage { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eR32G32B32A32Sfloat,
		vk::Extent3D { eqmapImageExtent, 1 },
		vku::Image::maxMipLevels(eqmapImageExtent), 1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled /* cubemap generation */ | vk::ImageUsageFlagBits::eTransferSrc /* mipmap generation */,
		gpu.queueFamilies.getUniqueIndices().size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
		vku::unsafeProxy(gpu.queueFamilies.getUniqueIndices()),
	} };

	{
		const auto transferCommandPool = createCommandPool(gpu.device, gpu.queueFamilies.transfer);
		std::unique_ptr<vku::AllocatedBuffer> eqmapStagingBuffer;
		vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
			eqmapStagingBuffer = std::make_unique<vku::AllocatedBuffer>(vku::MappedBuffer {
				gpu.allocator,
				std::from_range, io::StbDecoder<float>::fromFile(std::getenv("EQMAP_PATH"), 4).asSpan(),
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
		});
		gpu.queues.transfer.waitIdle();
	}

	const auto graphicsCommandPool = createCommandPool(gpu.device, gpu.queueFamilies.graphicsPresent);
	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		// Generate eqmapImage mipmaps.
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
			{}, {}, {},
			vk::ImageMemoryBarrier {
				{}, vk::AccessFlagBits::eTransferRead,
				vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
				vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
				eqmapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
			});
		recordMipmapGenerationCommand(cb, eqmapImage);

		// Blit from eqmapImage to reducedEqmapImage.
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
					vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead,
					{}, vk::ImageLayout::eTransferDstOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					reducedEqmapImage, vku::fullSubresourceRange(),
				},
			}),
		});

		// Calculate the blitting mip level ranges by comparing eqmapImage's extent and reducedEqmapImage's extent.
		std::uint32_t blitStartMipLevel = 0;
		for (std::uint32_t width = eqmapImage.extent.width; width != reducedEqmapImage.extent.width; width >>= 1, ++blitStartMipLevel);

		cb.blitImage(
			eqmapImage, vk::ImageLayout::eTransferSrcOptimal,
			reducedEqmapImage, vk::ImageLayout::eTransferDstOptimal,
			std::views::iota(0U, reducedEqmapImage.mipLevels)
				| std::views::transform([&](auto mipLevel) {
					return vk::ImageBlit {
						{ vk::ImageAspectFlagBits::eColor, blitStartMipLevel + mipLevel, 0, 1 },
						{ vk::Offset3D{}, vk::Offset3D { vku::toOffset2D(eqmapImage.mipExtent(blitStartMipLevel + mipLevel)), 1 } },
						{ vk::ImageAspectFlagBits::eColor, mipLevel, 0, 1 },
						{ vk::Offset3D{}, vk::Offset3D { vku::toOffset2D(reducedEqmapImage.mipExtent(mipLevel)), 1 } },
					};
				})
				| std::ranges::to<std::vector>(),
			vk::Filter::eLinear);

		// eqmapImage and reducedEqmapImage will be used as sampled image.
		cb.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, {}, {},
			{
				vk::ImageMemoryBarrier {
					vk::AccessFlagBits::eTransferRead, {},
					{}, vk::ImageLayout::eShaderReadOnlyOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					eqmapImage, vku::fullSubresourceRange(),
				},
				vk::ImageMemoryBarrier {
					vk::AccessFlagBits::eTransferWrite, {},
					vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
					vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
					reducedEqmapImage, vku::fullSubresourceRange(),
				},
			});
	});

	// Generate IBL resources.
	{
    	shaderc::Compiler compiler;

		// Create image view for eqmapImage.
		const vk::raii::ImageView eqmapImageView { gpu.device, eqmapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) };

		const pbrenvmap::Generator::Config config {
			.cubemap = { .usage = vk::ImageUsageFlagBits::eSampled },
			.sphericalHarmonicCoefficients = { .usage = vk::BufferUsageFlagBits::eUniformBuffer },
			.prefilteredmap = { .usage = vk::ImageUsageFlagBits::eSampled },
		};
		const pbrenvmap::Generator::Pipelines pbrenvmapPipelines {
			.cubemapComputer = { gpu.device, compiler },
			.subgroupMipmapComputer = { gpu.device, vku::Image::maxMipLevels(config.cubemap.size), 32U /* TODO */, compiler },
			.sphericalHarmonicsComputer = { gpu.device, compiler },
			.sphericalHarmonicCoefficientsSumComputer = { gpu.device, compiler },
			.prefilteredmapComputer = { gpu.device, { vku::Image::maxMipLevels(config.prefilteredmap.size), config.prefilteredmap.roughnessLevels }, compiler },
			.multiplyComputer = { gpu.device, compiler },
		};
		pbrenvmap::Generator pbrenvmapGenerator { gpu.device, gpu.allocator, config };

		const vulkan::pipeline::BrdfmapComputer brdfmapComputer { gpu.device };

		const vk::raii::DescriptorPool descriptorPool {
			gpu.device,
			brdfmapComputer.descriptorSetLayout.getPoolSize().getDescriptorPoolCreateInfo(),
		};

		const auto [brdfmapSets] = allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(brdfmapComputer.descriptorSetLayout));
		gpu.device.updateDescriptorSets(
			brdfmapSets.getWrite<0>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *brdfmapImageView, vk::ImageLayout::eGeneral })),
			{});

		const auto computeCommandPool = createCommandPool(gpu.device, gpu.queueFamilies.compute);
		vku::executeSingleCommand(*gpu.device, *computeCommandPool, gpu.queues.compute, [&](vk::CommandBuffer cb) {
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
				{
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

		vk::raii::ImageView cubemapImageView { gpu.device, pbrenvmapGenerator.cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
		vk::raii::ImageView prefilteredmapImageView { gpu.device, pbrenvmapGenerator.prefilteredmapImage.getViewCreateInfo(vk::ImageViewType::eCube) };

		imageBasedLightingResources.emplace(
		    std::move(pbrenvmapGenerator.cubemapImage),
		    std::move(cubemapImageView),
		    std::move(pbrenvmapGenerator.sphericalHarmonicCoefficientsBuffer),
		    std::move(pbrenvmapGenerator.prefilteredmapImage),
		    std::move(prefilteredmapImageView));

		std::array<glm::vec3, 9> sphericalHarmonicCoefficients;
		std::ranges::copy_n(pbrenvmapGenerator.sphericalHarmonicCoefficientsBuffer.asRange<const glm::vec3>().data(), 9, sphericalHarmonicCoefficients.data());

		appState.imageBasedLightingProperties = AppState::ImageBasedLighting {
			.eqmap = {
				.path = std::getenv("EQMAP_PATH"),
				.dimension = { eqmapImage.extent.width, eqmapImage.extent.height },
			},
			.cubemap = {
				.size = config.cubemap.size,
			},
			.diffuseIrradiance = {
				sphericalHarmonicCoefficients,
			},
			.prefilteredmap = {
				.size = config.prefilteredmap.size,
				.roughnessLevels = config.prefilteredmap.roughnessLevels,
				.sampleCount = config.prefilteredmap.samples,
			}
		};
	}

	vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
		// Acquire resource queue family ownerships.
		std::vector<vk::ImageMemoryBarrier2> memoryBarriers;
		if (gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
			memoryBarriers.append_range(assetResources.images | std::views::transform([&](vk::Image image) {
				return vk::ImageMemoryBarrier2 {
					{}, {},
					vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
					{}, {},
					gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
					image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
				};
			}));
		}
		if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
			memoryBarriers.push_back({
				{}, {},
				vk::PipelineStageFlagBits2::eAllCommands, {},
				vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
				gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
				imageBasedLightingResources.value().cubemapImage, vku::fullSubresourceRange(),
			});
			memoryBarriers.push_back({
				{}, {},
				vk::PipelineStageFlagBits2::eAllCommands, {},
				vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
				gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
				imageBasedLightingResources.value().prefilteredmapImage, vku::fullSubresourceRange(),
			});
			memoryBarriers.push_back({
				{}, {},
				vk::PipelineStageFlagBits2::eAllCommands, {},
				vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
				gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
				brdfmapImage, vku::fullSubresourceRange(),
			});
		}
		if (!memoryBarriers.empty()) {
			cb.pipelineBarrier2KHR({ {}, {}, {}, memoryBarriers });
		}

		if (assetResources.images.empty()) return;

		// Generate asset resource images' mipmaps.
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

	eqmapImageImGuiDescriptorSet = ImGui_ImplVulkan_AddTexture(*reducedEqmapSampler, *reducedEqmapImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
	ImGui_ImplVulkan_RemoveTexture(eqmapImageImGuiDescriptorSet);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

auto vk_gltf_viewer::MainApp::run() -> void {
	const glm::u32vec2 framebufferSize = window.getFramebufferSize();
	vulkan::SharedData sharedData { assetExpected.get(), gpu, window.getSurface(), vk::Extent2D { framebufferSize.x, framebufferSize.y } };
	std::array frames = ARRAY_OF(2, vulkan::Frame{ gpu, sharedData, assetResources, sceneResources });

	// Optionals that indicates frame should handle swapchain resize to the extent at the corresponding index.
	std::array<std::optional<vk::Extent2D>, std::tuple_size_v<decltype(frames)>> shouldHandleSwapchainResize{};

	const vk::raii::DescriptorPool descriptorPool {
		gpu.device,
		getPoolSizes(
			sharedData.imageBasedLightingDescriptorSetLayout,
			sharedData.assetDescriptorSetLayout,
			sharedData.sceneDescriptorSetLayout,
			sharedData.skyboxRenderer.descriptorSetLayout)
		.getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind),
	};
	const auto [imageBasedLightingDescriptorSet, assetDescriptorSet, sceneDescriptorSet, skyboxDescriptorSet]
		= allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(
			// TODO: requiring explicit const cast looks bad. vku::allocateDescriptorSets signature should be fixed.
			std::as_const(sharedData.imageBasedLightingDescriptorSetLayout),
			std::as_const(sharedData.assetDescriptorSetLayout),
			std::as_const(sharedData.sceneDescriptorSetLayout),
			std::as_const(sharedData.skyboxRenderer.descriptorSetLayout)));

	std::vector<vk::DescriptorImageInfo> assetTextures;
	assetTextures.emplace_back(*sharedData.singleTexelSampler, *sharedData.gltfFallbackImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
	assetTextures.append_range(assetResources.textures);

	gpu.device.updateDescriptorSets({
		imageBasedLightingDescriptorSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorBufferInfo { imageBasedLightingResources->cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize })),
		imageBasedLightingDescriptorSet.getWrite<1>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *imageBasedLightingResources->prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
		imageBasedLightingDescriptorSet.getWrite<2>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
		assetDescriptorSet.getWrite<0>(assetTextures),
		assetDescriptorSet.getWrite<1>(vku::unsafeProxy(vk::DescriptorBufferInfo { assetResources.materialBuffer, 0, vk::WholeSize })),
		sceneDescriptorSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorBufferInfo { sceneResources.primitiveBuffer, 0, vk::WholeSize })),
		sceneDescriptorSet.getWrite<1>(vku::unsafeProxy(vk::DescriptorBufferInfo { sceneResources.nodeTransformBuffer, 0, vk::WholeSize })),
		skyboxDescriptorSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *imageBasedLightingResources->cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
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

		control::imgui::inputControlSetting(appState);

		control::imgui::hdriEnvironments(eqmapImageImGuiDescriptorSet, appState);

		// Asset inspection.
		fastgltf::Asset &asset = assetExpected.get();
		const auto assetDir = std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path();
		control::imgui::assetInfos(asset);
		control::imgui::assetBufferViews(asset);
		control::imgui::assetBuffers(asset, assetDir);
		control::imgui::assetImages(asset, assetDir);
		control::imgui::assetSamplers(asset);
		control::imgui::assetMaterials(asset, assetTextureDescriptorSets);
		control::imgui::assetSceneHierarchies(asset, appState);

		// Node inspection.
		control::imgui::nodeInspector(assetExpected.get(), appState);

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
			.hoveringNodeIndex = appState.hoveringNodeIndex,
			.selectedNodeIndices = appState.selectedNodeIndices,
			.renderingNodeIndices = appState.renderingNodeIndices,
			.hoveringNodeOutline = appState.hoveringNodeOutline.to_optional(),
			.selectedNodeOutline = appState.selectedNodeOutline.to_optional(),
			.imageBasedLightingDescriptorSet = imageBasedLightingDescriptorSet,
			.assetDescriptorSet = assetDescriptorSet,
			.sceneDescriptorSet = sceneDescriptorSet,
			.skyboxDescriptorSet = skyboxDescriptorSet,
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
		vku::unsafeProxy({
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

auto vk_gltf_viewer::MainApp::createReducedEqmapImage(
	const vk::Extent2D &eqmapImageExtent
) -> vku::AllocatedImage {
	vk::Extent2D reducedExtent = eqmapImageExtent;
	while (reducedExtent.width <= 512) {
		reducedExtent.width <<= 1;
		reducedExtent.height <<= 1;
	}

	return { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eB10G11R11UfloatPack32,
		vk::Extent3D { reducedExtent, 1 },
		vku::Image::maxMipLevels(reducedExtent), 1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
	} };
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

auto vk_gltf_viewer::MainApp::createImGuiDescriptorPool() const -> decltype(imGuiDescriptorPool) {
	return { gpu.device, vk::DescriptorPoolCreateInfo {
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		1 /* Default ImGui rendering */
			+ 1 /* equirectangular texture */
			+ static_cast<std::uint32_t>(assetResources.textures.size()) /* material textures */,
		vku::unsafeProxy({
			vk::DescriptorPoolSize {
				vk::DescriptorType::eCombinedImageSampler,
				1 /* Default ImGui rendering */
					+ 1 /* equirectangular texture */
					+ static_cast<std::uint32_t>(assetResources.textures.size()) /* material textures */
			},
		}),
	} };
}