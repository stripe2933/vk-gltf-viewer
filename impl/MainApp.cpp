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
import :control.ImGui;
import :helpers.ranges;
import :io.StbDecoder;
import :mipmap;
import :vulkan.Frame;
import :vulkan.generator.ImageBasedLightingResourceGenerator;
import :vulkan.generator.MipmappedCubemapGenerator;
import :vulkan.pipeline.BrdfmapComputer;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

template <typename ...Fs>
struct multilambda : Fs... {
	using Fs::operator()...;
};

template <std::invocable<vk::CommandBuffer> F>
struct ExecutionInfo {
    F commandRecorder;
    vk::CommandPool commandPool;
    vk::Queue queue;
    std::optional<std::uint64_t> signalValue { 0ULL };
};

template <typename Key, typename ValueConstructor, typename Value = std::invoke_result_t<ValueConstructor>>
class OnDemandCounterStorage {
public:
    explicit OnDemandCounterStorage(ValueConstructor &&_valueConstructor)
        : valueConstructor { FWD(_valueConstructor) } { }

    [[nodiscard]] auto at(const Key &k) noexcept -> const Value& {
        if (auto index = counter[k]++; index < valueStorage.size()) {
            return valueStorage[index];
        }

        return valueStorage.emplace_back(valueConstructor());
    }

    [[nodiscard]] auto getValueStorage() const noexcept -> const std::deque<Value>& { return valueStorage; }
    [[nodiscard]] auto getValueStorage() noexcept -> std::deque<Value>& { return valueStorage; }

private:
    ValueConstructor valueConstructor;
    std::deque<Value> valueStorage;
    std::unordered_map<Key, std::uint32_t> counter;
};

template <typename Key, typename ValueConstructor>
[[nodiscard]] auto makeOnDemandCounterStorage(ValueConstructor &&valueConstructor) -> OnDemandCounterStorage<Key, ValueConstructor> {
    return OnDemandCounterStorage<Key, ValueConstructor> { FWD(valueConstructor) };
}

template <typename... ExecutionInfoTuples>
[[nodiscard]] auto executeHierarchicalCommands(
    const vk::raii::Device &device,
    ExecutionInfoTuples &&...executionInfoTuples
) -> std::pair<std::vector<vk::raii::Semaphore>, std::vector<std::uint64_t>> {
    // Count the total required command buffers for each command pool.
    std::unordered_map<vk::CommandPool, std::uint32_t> commandBufferCounts;
    ([&]() {
        apply([&](const auto &...executionInfo) {
            (++commandBufferCounts[executionInfo.commandPool], ...);
        }, executionInfoTuples);
    }(), ...);

    // Make FIFO command buffer queue for each command pools. When all command buffers are submitted, they must be empty.
    std::unordered_map<vk::CommandPool, std::queue<vk::CommandBuffer>> commandBufferQueues;
    for (auto [commandPool, commandbufferCount] : commandBufferCounts) {
        commandBufferQueues.emplace(
            std::piecewise_construct,
            std::tuple { commandPool },
            std::forward_as_tuple(std::from_range, (*device).allocateCommandBuffers({
                commandPool,
                vk::CommandBufferLevel::ePrimary,
                commandbufferCount,
            })));
    }

    OnDemandCounterStorage timelineSemaphores = makeOnDemandCounterStorage<std::uint64_t>([&] -> vk::raii::Semaphore {
        return { device, vk::StructureChain {
            vk::SemaphoreCreateInfo{},
            vk::SemaphoreTypeCreateInfo { vk::SemaphoreType::eTimeline, 0 },
        }.get() };
    });
    std::unordered_map<vk::Semaphore, std::uint64_t> finalSignalSemaphoreValues;

    INDEX_SEQ(Is, sizeof...(ExecutionInfoTuples), {
        // Collect the submission command buffers and the signal semaphore by 1) destination queue, 2) wait semaphore
        // value, 3) signal semaphore value (if exist).
        std::map<std::tuple<vk::Queue, std::uint64_t, std::optional<std::uint64_t>>, std::pair<std::vector<vk::CommandBuffer>, vk::Semaphore>> submitInfos;
        std::unordered_multimap<std::uint64_t, vk::Semaphore> waitSemaphoresPerSignalValues;

        // Execute the following lambda for every executionInfoTuples parameter packs.
        ([&]() {
            constexpr std::uint64_t waitSemaphoreValue = Is;
            constexpr std::uint64_t signalSemaphoreValue = Is + 1;

            apply([&](auto &...executionInfo) {
                // Execute the following lambda for every executionInfo entry in executionInfoTuples.
                ([&]() {
                    // Get command buffer from FIFO queue and pop it.
                    auto &dedicatedCommandBufferQueue = commandBufferQueues[executionInfo.commandPool];
                    vk::CommandBuffer commandBuffer = dedicatedCommandBufferQueue.front();
                    dedicatedCommandBufferQueue.pop();

                    // Record commands into the commandBuffer by executing executionInfo.commandRecorder.
                    commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
                    executionInfo.commandRecorder(commandBuffer);
                    commandBuffer.end();

                    // Push commandBuffer into the corresponding submitInfos entry.
                    const std::tuple key {
                        executionInfo.queue,
                        waitSemaphoreValue,
                        executionInfo.signalValue.transform([](auto v) { return v == 0 ? signalSemaphoreValue : v; }),
                    };
                    auto it = submitInfos.find(key);
                    if (it == submitInfos.end()) {
                        vk::Semaphore signalSemaphore = nullptr;
                        if (get<2>(key) /* modified signal value */) {
                            // Register the semaphore for a submission whose waitSemaphoreValue is current's signalSemaphoreValue.
                            signalSemaphore = *timelineSemaphores.at(*get<2>(key));
                            waitSemaphoresPerSignalValues.emplace(signalSemaphoreValue, signalSemaphore);
                        }

                        it = submitInfos.emplace_hint(it, key, std::pair { std::vector<vk::CommandBuffer>{}, signalSemaphore });
                    }
                    it->second.first/*commandBuffers*/.push_back(commandBuffer);
                }(), ...);
            }, executionInfoTuples);
        }(), ...);

        struct TimelineSemaphoreWaitInfo {
            std::vector<vk::Semaphore> waitSemaphores;
            std::vector<std::uint64_t> waitSemaphoreValues;

            TimelineSemaphoreWaitInfo(std::vector<vk::Semaphore> _waitSemaphores, std::uint64_t waitValue)
                : waitSemaphores { std::move(_waitSemaphores) }
                , waitSemaphoreValues { std::vector<std::uint64_t>(waitSemaphores.size(), waitValue) } { }
        };

        std::unordered_map<vk::Queue, std::vector<vk::SubmitInfo>> submitInfosPerQueue;
        std::vector<TimelineSemaphoreWaitInfo> waitInfos;
        std::list<vk::TimelineSemaphoreSubmitInfo> timelineSemaphoreSubmitInfos;
        // Total dstStageMasks does not exceed the total wait semaphore count (=timelineSemaphores.getValueStorage().size()).
        const std::vector waitDstStageMasks(timelineSemaphores.getValueStorage().size(), vk::Flags { vk::PipelineStageFlagBits::eTopOfPipe });

        for (const auto &[key, value] : submitInfos) {
            const auto &[queue, waitSemaphoreValue, signalSemaphoreValue] = key;
            const auto &[commandBuffers, signalSemaphore] = value;

            constexpr auto make_subrange = []<typename It>(std::pair<It, It> pairs) {
                return std::ranges::subrange(pairs.first, pairs.second);
            };
            const auto &[waitSemaphores, waitSemaphoreValues] = waitInfos.emplace_back(
                make_subrange(waitSemaphoresPerSignalValues.equal_range(waitSemaphoreValue)) | std::views::values | std::ranges::to<std::vector>(),
                waitSemaphoreValue);

            if (signalSemaphoreValue) {
                submitInfosPerQueue[queue].emplace_back(
                    waitSemaphores,
                    vku::unsafeProxy(std::span { waitDstStageMasks }.subspan(0, waitSemaphores.size())),
                    commandBuffers,
                    signalSemaphore,
                    &timelineSemaphoreSubmitInfos.emplace_back(waitSemaphoreValues, *signalSemaphoreValue));

                // Update the final signal semaphore value for current signal semaphore,
                // i.e. update the value to current value if value < current value.
                std::uint64_t &finalSignalValue = finalSignalSemaphoreValues[signalSemaphore];
                finalSignalValue = std::max(finalSignalValue, *signalSemaphoreValue);
            }
            else if (waitSemaphores.empty()) {
                // Don't need to use vk::TimelineSemaphoreSubmitInfo.
                submitInfosPerQueue[queue].push_back({ {}, {}, commandBuffers });
            }
            else {
                submitInfosPerQueue[queue].push_back({
                    waitSemaphores,
                    vku::unsafeProxy(std::span { waitDstStageMasks }.subspan(0, waitSemaphores.size())),
                    commandBuffers,
                    {},
                    &timelineSemaphoreSubmitInfos.emplace_back(waitSemaphoreValues),
                });
            }
        }

        for (const auto &[queue, submitInfos] : submitInfosPerQueue) {
            queue.submit(submitInfos);
        }
    });

    std::pair<std::vector<vk::raii::Semaphore>, std::vector<std::uint64_t>> result;
    for (vk::raii::Semaphore &timelineSemaphore : timelineSemaphores.getValueStorage()) {
        result.second.push_back(finalSignalSemaphoreValues[*timelineSemaphore]);
        result.first.push_back(std::move(timelineSemaphore));
    }
    return result;
}

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

	const auto [timelineSemaphores, finalWaitValues] = executeHierarchicalCommands(
		gpu.device,
		std::forward_as_tuple(
			// Create BRDF LUT image.
			ExecutionInfo { [&](vk::CommandBuffer cb) {
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
			ExecutionInfo { [&](vk::CommandBuffer cb) {
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
			ExecutionInfo { [&](vk::CommandBuffer cb) {
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

	processEqmapChange(std::getenv("EQMAP_PATH"), false);

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
	vulkan::SharedData sharedData {
		assetExpected.get(),
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
			[&](const control::imgui::task::LoadEqmap &task) {
				processEqmapChange(task.path, true);
			},
			[](std::monostate) { },
		}, control::imgui::menuBar());

		control::imgui::inputControlSetting(appState);

		control::imgui::skybox(appState);
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
			.imageBasedLightingDescriptorSet = imageBasedLightingResources.value() /*TODO*/.descriptorSet,
			.assetDescriptorSet = assetDescriptorSet,
			.sceneDescriptorSet = sceneDescriptorSet,
			.background = appState.background.to_optional()
				.and_then([&](const glm::vec3 &color) {
					return std::optional<decltype(vulkan::Frame::ExecutionTask::background)> { color };
				})
				.value_or(decltype(vulkan::Frame::ExecutionTask::background) { std::in_place_index<1>, skyboxResources.value() /*TODO*/.descriptorSet }),
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

auto vk_gltf_viewer::MainApp::createReducedEqmapImage(
	const vk::Extent2D &eqmapLastMipImageExtent
) -> vku::AllocatedImage {
	return { gpu.allocator, vk::ImageCreateInfo {
		{},
		vk::ImageType::e2D,
		vk::Format::eB10G11R11UfloatPack32,
		vk::Extent3D { eqmapLastMipImageExtent, 1 },
		vku::Image::maxMipLevels(eqmapLastMipImageExtent), 1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
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

auto vk_gltf_viewer::MainApp::processEqmapChange(
	const std::filesystem::path &eqmapPath,
	bool usePreviousDescriptorSets
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

	vulkan::MipmappedCubemapGenerator mippedCubemapGenerator { gpu, {
		.cubemapSize = 1024,
		.cubemapUsage = vk::ImageUsageFlagBits::eSampled,
	} };
	const vulkan::MipmappedCubemapGenerator::Pipelines mippedCubemapGeneratorPipelines {
		vulkan::pipeline::CubemapComputer { gpu.device },
		vulkan::pipeline::SubgroupMipmapComputer { gpu.device, mippedCubemapGenerator.cubemapImage.mipLevels, 32 /*TODO: use proper subgroup size!*/ },
	};

	// Generate IBL resources.
	vulkan::ImageBasedLightingResourceGenerator iblGenerator { gpu, {
		.prefilteredmapImageUsage = vk::ImageUsageFlagBits::eSampled,
		.sphericalHarmonicsBufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
	} };
	const vulkan::ImageBasedLightingResourceGenerator::Pipelines iblGeneratorPipelines {
		vulkan::pipeline::PrefilteredmapComputer { gpu.device, { iblGenerator.prefilteredmapImage.mipLevels, 1024 } },
		vulkan::pipeline::SphericalHarmonicsComputer { gpu.device },
		vulkan::pipeline::SphericalHarmonicCoefficientsSumComputer { gpu.device },
		vulkan::pipeline::MultiplyComputer { gpu.device },
	};

	const auto [timelineSemaphores, finalWaitValues] = executeHierarchicalCommands(
		gpu.device,
		std::forward_as_tuple(
			// Create device-local eqmap image from staging buffer.
			ExecutionInfo { [&](vk::CommandBuffer cb) {
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
			ExecutionInfo { [&](vk::CommandBuffer cb) {
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
			ExecutionInfo { [&](vk::CommandBuffer cb) {
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
			ExecutionInfo { [&](vk::CommandBuffer cb) {
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
			ExecutionInfo { [&](vk::CommandBuffer cb) {
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

	auto [imageBasedLightingDescriptorSet, skyboxDescriptorSet] = [&]() {
		if (usePreviousDescriptorSets){
			assert(imageBasedLightingResources.has_value() && "No pre-allocated descriptor set in imageBasedLightingResources");
			assert(skyboxResources.has_value() && "No pre-allocated descriptor set in skyboxResources");

			return std::tuple { imageBasedLightingResources->descriptorSet, skyboxResources->descriptorSet };
		}
		else {
			return allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(
				// TODO: requiring explicit const cast looks bad. vku::allocateDescriptorSets signature should be fixed.
				std::as_const(imageBasedLightingDescriptorSetLayout),
				std::as_const(skyboxDescriptorSetLayout)));
		}
	}();

	vk::raii::ImageView cubemapImageView { gpu.device, mippedCubemapGenerator.cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
	skyboxResources.emplace(
		std::move(mippedCubemapGenerator.cubemapImage),
		std::move(cubemapImageView),
		skyboxDescriptorSet);

	vk::raii::ImageView prefilteredmapImageView { gpu.device, iblGenerator.prefilteredmapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
	imageBasedLightingResources.emplace(
		std::move(iblGenerator.sphericalHarmonicsBuffer),
		std::move(iblGenerator.prefilteredmapImage),
		std::move(prefilteredmapImageView),
		imageBasedLightingDescriptorSet);

	std::array<glm::vec3, 9> sphericalHarmonicCoefficients;
	std::ranges::copy_n(imageBasedLightingResources->cubemapSphericalHarmonicsBuffer.asRange<const glm::vec3>().data(), 9, sphericalHarmonicCoefficients.data());

	appState.imageBasedLightingProperties = AppState::ImageBasedLighting {
		.eqmap = {
			.path = eqmapPath,
			.dimension = { eqmapImage.extent.width, eqmapImage.extent.height },
		},
		.cubemap = {
			.size = skyboxResources->cubemapImage.extent.width,
		},
		.diffuseIrradiance = {
			sphericalHarmonicCoefficients,
		},
		.prefilteredmap = {
			.size = imageBasedLightingResources->prefilteredmapImage.extent.width,
			.roughnessLevels = imageBasedLightingResources->prefilteredmapImage.mipLevels,
			.sampleCount = 1024,
		}
	};

	gpu.device.updateDescriptorSets({
		imageBasedLightingDescriptorSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorBufferInfo { imageBasedLightingResources->cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize })),
		imageBasedLightingDescriptorSet.getWrite<1>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *imageBasedLightingResources->prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
		imageBasedLightingDescriptorSet.getWrite<2>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
		skyboxDescriptorSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *skyboxResources->cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
	}, {});
}
