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
import ranges;
import vku;
import :control.ImGui;
import :helpers.functional;
import :io.StbDecoder;
import :vulkan.Frame;
import :vulkan.generator.ImageBasedLightingResourceGenerator;
import :vulkan.generator.MipmappedCubemapGenerator;
import :vulkan.mipmap;
import :vulkan.pipeline.BrdfmapComputer;
import :vulkan.pipeline.CubemapToneMappingRenderer;

vk_gltf_viewer::MainApp::MainApp() {
    const vulkan::pipeline::BrdfmapComputer brdfmapComputer { gpu.device };

    const vk::raii::DescriptorPool descriptorPool {
        gpu.device,
        brdfmapComputer.descriptorSetLayout.getPoolSize().getDescriptorPoolCreateInfo(),
    };

    const auto [brdfmapSet] = allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(brdfmapComputer.descriptorSetLayout));
    gpu.device.updateDescriptorSets(
        brdfmapSet.getWriteOne<0>({ {}, *brdfmapImageView, vk::ImageLayout::eGeneral }),
        {});

    const vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
    const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };

    const auto [timelineSemaphores, finalWaitValues] = vku::executeHierarchicalCommands(
        gpu.device,
        std::forward_as_tuple(
            // Clear asset fallback image by white.
            vku::ExecutionInfo { [this](vk::CommandBuffer cb) {
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                    {}, {}, {},
                    vk::ImageMemoryBarrier {
                        {}, vk::AccessFlagBits::eTransferWrite,
                        {}, vk::ImageLayout::eTransferDstOptimal,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        assetFallbackImage, vku::fullSubresourceRange(),
                    });
                cb.clearColorImage(
                    assetFallbackImage, vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue { 1.f, 1.f, 1.f, 1.f },
                    vku::fullSubresourceRange());
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
                    {}, {}, {},
                    vk::ImageMemoryBarrier {
                        vk::AccessFlagBits::eTransferWrite, {},
                        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        assetFallbackImage, vku::fullSubresourceRange(),
                    });
            }, *graphicsCommandPool, gpu.queues.graphicsPresent/*, 2*/ },
            // Initialize the image based lighting resources by default(white).
            vku::ExecutionInfo { [this](vk::CommandBuffer cb) {
                // Clear prefilteredmapImage to white.
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                    {}, {}, {},
                    vk::ImageMemoryBarrier {
                        {}, vk::AccessFlagBits::eTransferWrite,
                        {}, vk::ImageLayout::eTransferDstOptimal,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        imageBasedLightingResources.prefilteredmapImage, vku::fullSubresourceRange(),
                    });
                cb.clearColorImage(
                    imageBasedLightingResources.prefilteredmapImage, vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue { 1.f, 1.f, 1.f, 0.f },
                    vku::fullSubresourceRange());
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
                    {}, {}, {},
                    vk::ImageMemoryBarrier {
                        vk::AccessFlagBits::eTransferWrite, {},
                        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        imageBasedLightingResources.prefilteredmapImage, vku::fullSubresourceRange(),
                    });
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
            }, *computeCommandPool, gpu.queues.compute }),
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
    }, ~0ULL);
    if (semaphoreWaitResult != vk::Result::eSuccess) {
        throw std::runtime_error { "Failed to launch application!" };
    }

    gpu.device.updateDescriptorSets({
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<0>({ imageBasedLightingResources.cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<1>({ {}, *imageBasedLightingResources.prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<2>({ {}, *brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
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

    ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddChar(0x2197 /*↗*/);
    builder.BuildRanges(&ranges);
    io.Fonts->AddFontFromFileTTF("/Library/Fonts/Arial Unicode.ttf", 16.f * io.DisplayFramebufferScale.x, nullptr, ranges.Data);
    io.Fonts->Build();

    ImGui_ImplGlfw_InitForVulkan(window, true);
    const auto colorAttachmentFormats = { gpu.supportSwapchainMutableFormat ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb };
    ImGui_ImplVulkan_InitInfo initInfo {
        .Instance = *instance,
        .PhysicalDevice = *gpu.physicalDevice,
        .Device = *gpu.device,
        .Queue = gpu.queues.graphicsPresent,
        .DescriptorPool = vku::toCType(*imGuiDescriptorPool),
        .MinImageCount = 2,
        .ImageCount = 2,
        .UseDynamicRendering = true,
        .PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfo {
            {},
            colorAttachmentFormats,
        },
    };
    ImGui_ImplVulkan_Init(&initInfo);
}

vk_gltf_viewer::MainApp::~MainApp() {
    for (vk::DescriptorSet textureDescriptorSet : assetTextureDescriptorSets) {
        ImGui_ImplVulkan_RemoveTexture(vku::toCType(textureDescriptorSet));
    }
    if (skyboxResources) {
        ImGui_ImplVulkan_RemoveTexture(vku::toCType(skyboxResources->imGuiEqmapTextureDescriptorSet));
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

auto vk_gltf_viewer::MainApp::run() -> void {
    // Booleans that indicates frame at the corresponding index should handle swapchain resizing.
    std::array<bool, std::tuple_size_v<decltype(frames)>> shouldHandleSwapchainResize{};

    float elapsedTime = 0.f;
    for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); ++frameIndex) {
        // Wait for previous frame execution to end.
        frames[frameIndex % frames.size()].waitForPreviousExecution();

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

        // Assign the passthruRect to appState.passthruRect. Handle stuffs that are dependent to it.
        static vk::Rect2D previousPassthruRect{};
        if (vk::Rect2D oldPassthruRect = std::exchange(previousPassthruRect, passthruRect); oldPassthruRect != passthruRect) {
            appState.camera.aspectRatio = vku::aspect(passthruRect.extent);
        }

        // Draw main menu bar.
        visit(multilambda {
            [&](const control::imgui::task::LoadGltf &task) {
                // TODO: I'm aware that there are more good solutions than waitIdle, but I don't have much time for it
                //  so I'll just use it for now.
                gpu.device.waitIdle();

                gltfAsset.emplace(task.path, gpu);

                sharedData.updateTextureCount(1 + gltfAsset->get().textures.size());

                gpu.device.updateDescriptorSets({
                    sharedData.assetDescriptorSet.getWrite<0>(vku::unsafeProxy([&]() {
                        std::vector<vk::DescriptorImageInfo> imageInfos;
                        imageInfos.reserve(1 + gltfAsset->get().textures.size());

                        imageInfos.emplace_back(*sharedData.singleTexelSampler, *assetFallbackImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
                        imageInfos.append_range(gltfAsset->get().textures | std::views::transform([this](const fastgltf::Texture &texture) {
                            return vk::DescriptorImageInfo {
                                [&]() {
                                    if (texture.samplerIndex) return *gltfAsset->assetTextures.samplers[*texture.samplerIndex];
                                    return *assetDefaultSampler;
                                }(),
                                *gltfAsset->imageViews.at(gltf::AssetTextures::getPreferredImageIndex(texture)),
                                vk::ImageLayout::eShaderReadOnlyOptimal,
                            };
                        }));

                        return imageInfos;
                    }())),
                    sharedData.assetDescriptorSet.getWriteOne<1>({ gltfAsset->assetResources.materialBuffer, 0, vk::WholeSize }),
                    sharedData.sceneDescriptorSet.getWriteOne<0>({ gltfAsset->sceneResources.primitiveBuffer, 0, vk::WholeSize }),
                    sharedData.sceneDescriptorSet.getWriteOne<1>({ gltfAsset->sceneResources.nodeWorldTransformBuffer, 0, vk::WholeSize }),
                }, {});

                // TODO: due to the ImGui's gamma correction issue, base color/emissive texture is rendered darker than it should be.
                assetTextureDescriptorSets
                    = gltfAsset->get().textures
                    | std::views::transform([this](const fastgltf::Texture &texture) -> vk::DescriptorSet {
                        return static_cast<vk::DescriptorSet>(ImGui_ImplVulkan_AddTexture(
                            [&]() {
                                if (texture.samplerIndex) return vku::toCType(*gltfAsset->assetTextures.samplers[*texture.samplerIndex]);
                                return vku::toCType(*assetDefaultSampler);
                            }(),
                            vku::toCType(*gltfAsset->imageViews.at(gltf::AssetTextures::getPreferredImageIndex(texture))),
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
                    })
                    | std::ranges::to<std::vector>();

                // Update AppState.
                appState.gltfAsset.emplace(gltfAsset->get(), gltfAsset->assetDir);
            },
            [&](control::imgui::task::CloseGltf) {
                gltfAsset.reset();

                // Update AppState.
                appState.gltfAsset.reset();
            },
            [&](const control::imgui::task::LoadEqmap &task) {
                processEqmapChange(task.path);
            },
            [](std::monostate) { },
        }, control::imgui::menuBar(appState));

        control::imgui::inputControlSetting(appState);

        control::imgui::skybox(appState);
        if (skyboxResources) {
            control::imgui::hdriEnvironments(skyboxResources->imGuiEqmapTextureDescriptorSet, appState);
        }

        // Asset inspection.
        control::imgui::assetInfos(appState);
        control::imgui::assetBufferViews(appState);
        control::imgui::assetBuffers(appState);
        control::imgui::assetImages(appState);
        control::imgui::assetSamplers(appState);
        control::imgui::assetMaterials(appState, assetTextureDescriptorSets);
        control::imgui::assetSceneHierarchies(appState);

        // Node inspection.
        control::imgui::nodeInspector(appState);

        // ImGuizmo.
        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(centerNodeRect.Min.x, centerNodeRect.Min.y, centerNodeRect.GetWidth(), centerNodeRect.GetHeight());
        if (appState.canManipulateImGuizmo()) {
            assert(gltfAsset && "glTF asset stored in AppState but not in MainApp");
            const std::span nodeWorldTransforms = gltfAsset->sceneResources.nodeWorldTransformBuffer.asRange<const glm::mat4>();
            const std::size_t selectedNodeIndex = *appState.gltfAsset->selectedNodeIndices.begin();
            const glm::mat4 &nodeWorldTransform = nodeWorldTransforms[selectedNodeIndex];

            if (auto deltaMatrix = control::imgui::manipulate(appState, nodeWorldTransform)) {
                // If ImGuizmo manipulation is updated, update the target node transform in the asset.
                fastgltf::Asset &asset = gltfAsset->get();
                fastgltf::Node &deltaNode = asset.nodes[selectedNodeIndex];
                visit(multilambda {
                    [&](fastgltf::TRS &trs) {
                        // Convert TRS to mat4.
                        glm::mat4 newTransform = translate(glm::mat4 { 1.f }, glm::make_vec3(trs.translation.data()))
                            * mat4_cast(glm::make_quat(trs.rotation.data()))
                            * scale(glm::mat4 { 1.f }, glm::make_vec3(trs.scale.data()));

                        // Apply deltaMatrix.
                        newTransform *= *deltaMatrix;

                        // Convert mat4 to TRS.
                        fastgltf::Node::TransformMatrix transformMatrix;
                        std::copy_n(value_ptr(newTransform), 16, transformMatrix.data());
                        fastgltf::decomposeTransformMatrix(transformMatrix, trs.scale, trs.rotation, trs.translation);
                    },
                    [&](fastgltf::Node::TransformMatrix &transformMatrix) {
                        // Apply deltaMatrix.
                        glm::mat4 newTransform = glm::make_mat4(transformMatrix.data());
                        newTransform *= *deltaMatrix;
                        std::copy_n(value_ptr(newTransform), 16, transformMatrix.data());
                    },
                }, deltaNode.transform);

                // Recursively update the current's and child nodes' transform.
                // TODO: this must be done under sceneResources.nodeTransformBuffer is idle from GPU access.
                const auto calculateNodeTransformsRecursive
                    = [&, mutableNodeWorldTransforms = gltfAsset->sceneResources.nodeWorldTransformBuffer.asRange<glm::mat4>()](
                        this const auto &self,
                        std::size_t nodeIndex,
                        const glm::mat4 &parentNodeWorldTransform = { 1.f }
                    ) -> void {
                        const fastgltf::Node &node = asset.nodes[nodeIndex];
                        mutableNodeWorldTransforms[nodeIndex] = parentNodeWorldTransform * visit(fastgltf::visitor {
                            [](const fastgltf::TRS &trs) {
                                return translate(glm::mat4 { 1.f }, glm::make_vec3(trs.translation.data()))
                                    * mat4_cast(glm::make_quat(trs.rotation.data()))
                                    * scale(glm::mat4 { 1.f }, glm::make_vec3(trs.scale.data()));
                            },
                            [](const fastgltf::Node::TransformMatrix &mat) {
                                return glm::make_mat4(mat.data());
                            },
                        }, node.transform);

                        for (std::size_t childNodeIndex : node.children) {
                            self(childNodeIndex, mutableNodeWorldTransforms[nodeIndex]);
                        }
                    };

                // Start from the current selected node, execute calculateNodeTransformsRecursive with its parent node's
                // world transform. (Use identity matrix if selected node is root node.)
                const std::size_t parentNodeIndex = appState.gltfAsset->parentNodeIndices[selectedNodeIndex];
                const glm::mat4 parentNodeWorldTransform = parentNodeIndex == selectedNodeIndex
                    ? glm::mat4 { 1.f } : nodeWorldTransforms[parentNodeIndex];
                calculateNodeTransformsRecursive(selectedNodeIndex, parentNodeWorldTransform);
            }
        }
        control::imgui::viewManipulate(appState, centerNodeRect.Max);

        ImGui::Render();

        const vulkan::Frame::ExecutionTask task {
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
            .gltf = gltfAsset.transform([&](GltfAsset &gltfAsset) {
                assert(appState.gltfAsset && "Synchronization error: gltfAsset is not set in AppState.");
                return vulkan::Frame::ExecutionTask::Gltf {
                    .asset = gltfAsset.get(),
                    .indexBuffers = gltfAsset.assetResources.indexBuffers,
                    .sceneResources = gltfAsset.sceneResources,
                    .hoveringNodeIndex = appState.gltfAsset->hoveringNodeIndex,
                    .selectedNodeIndices = appState.gltfAsset->selectedNodeIndices,
                    .renderingNodeIndices = appState.gltfAsset->getVisibleNodeIndices(),
                };
            }),
            .solidBackground = appState.background.to_optional(),
            .handleSwapchainResize = std::exchange(shouldHandleSwapchainResize[frameIndex % frames.size()], false),
        };

        const vulkan::Frame::UpdateResult updateResult = frames[frameIndex % frames.size()].update(task);
        if (appState.gltfAsset) {
            appState.gltfAsset->hoveringNodeIndex = updateResult.hoveringNodeIndex;
        }

        if (!frames[frameIndex % frames.size()].execute()) {
            gpu.device.waitIdle();

            // Yield while window is minimized.
            glm::u32vec2 framebufferSize;
            while (!glfwWindowShouldClose(window) && (framebufferSize = window.getFramebufferSize()) == glm::u32vec2 { 0, 0 }) {
                std::this_thread::yield();
            }

            sharedData.handleSwapchainResize(window.getSurface(), { framebufferSize.x, framebufferSize.y });
            shouldHandleSwapchainResize.fill(true);
        }
    }
    gpu.device.waitIdle();
}

vk_gltf_viewer::MainApp::GltfAsset::DataBufferLoader::DataBufferLoader(const std::filesystem::path &path) {
    if (!dataBuffer.loadFromFile(path)) {
        throw std::runtime_error { "Failed to load glTF data buffer" };
    }
}

vk_gltf_viewer::MainApp::GltfAsset::GltfAsset(
    const std::filesystem::path &path,
    const vulkan::Gpu &gpu [[clang::lifetimebound]]
) : dataBufferLoader { path },
    assetDir { path.parent_path() },
    assetExpected { fastgltf::Parser { supportedExtensions }.loadGltf(&dataBufferLoader.dataBuffer, assetDir) },
    assetExternalBuffers { std::make_unique<gltf::AssetExternalBuffers>(get(), assetDir) },
    assetResources { get(), *assetExternalBuffers, gpu },
    assetTextures { get(), assetDir, *assetExternalBuffers, gpu },
    imageViews { createAssetImageViews(gpu.device) },
    sceneResources { get(), assetResources, get().scenes[get().defaultScene.value_or(0)], gpu } {
    assetExternalBuffers.reset(); // Drop the intermediate result that are not used in rendering.
}

auto vk_gltf_viewer::MainApp::GltfAsset::get() noexcept -> fastgltf::Asset& {
    return assetExpected.get();
}

auto vk_gltf_viewer::MainApp::GltfAsset::createAssetImageViews(
    const vk::raii::Device &device
) -> std::unordered_map<std::size_t, vk::raii::ImageView> {
    return assetTextures.images
        | ranges::views::value_transform([&](const vku::Image &image) -> vk::raii::ImageView {
            return { device, image.getViewCreateInfo() };
        })
        | std::ranges::to<std::unordered_map>();
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

auto vk_gltf_viewer::MainApp::createAssetFallbackImage() const -> vku::AllocatedImage {
    return { gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eR8G8B8A8Unorm,
        vk::Extent3D { 1, 1, 1 },
        1, 1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
    } };
}

auto vk_gltf_viewer::MainApp::createAssetDefaultSampler() const -> vk::raii::Sampler {
    return { gpu.device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        {}, {}, {},
        {},
        true, 16.f,
        {}, {},
        {}, vk::LodClampNone,
    } };
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

auto vk_gltf_viewer::MainApp::createImGuiDescriptorPool() -> decltype(imGuiDescriptorPool) {
    return { gpu.device, vk::DescriptorPoolCreateInfo {
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        1 /* Default ImGui rendering */
            + 1 /* reducedEqmapImage texture */
            + /*static_cast<std::uint32_t>(gltfAsset->get().textures.size())*/ 512 /* material textures */, // TODO: need proper texture count.
        vku::unsafeProxy({
            vk::DescriptorPoolSize {
                vk::DescriptorType::eCombinedImageSampler,
                1 /* Default ImGui rendering */
                    + 1 /* reducedEqmapImage texture */
                    + /*static_cast<std::uint32_t>(gltfAsset->get().textures.size())*/ 512 /* material textures */ // TODO: need proper texture count.
            },
        }),
    } };
}

auto vk_gltf_viewer::MainApp::processEqmapChange(
    const std::filesystem::path &eqmapPath
) -> void {
    // Load equirectangular map image and stage it into eqmapImage.
    int width, height;
    if (!stbi_info(eqmapPath.string().c_str(), &width, &height, nullptr)) {
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
        vulkan::pipeline::SubgroupMipmapComputer { gpu, vku::Image::maxMipLevels(mippedCubemapGeneratorConfig.cubemapSize) },
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

    // Generate Tone-mapped cubemap.
    const vulkan::rp::CubemapToneMapping cubemapToneMappingRenderPass { gpu.device };
    const vulkan::CubemapToneMappingRenderer cubemapToneMappingRenderer { gpu.device, {} /* TODO: reuse existing shader? */, cubemapToneMappingRenderPass };

    vku::AllocatedImage toneMappedCubemapImage { gpu.allocator, vk::ImageCreateInfo {
        vk::ImageCreateFlagBits::eCubeCompatible,
        vk::ImageType::e2D,
        vk::Format::eB8G8R8A8Srgb,
        mippedCubemapGenerator.cubemapImage.extent,
        1, mippedCubemapGenerator.cubemapImage.arrayLayers,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
    } };
    const vk::raii::ImageView cubemapImageArrayView {
        gpu.device,
        mippedCubemapGenerator.cubemapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 }, vk::ImageViewType::e2DArray),
    };
    const vk::raii::ImageView toneMappedCubemapImageArrayView { gpu.device, toneMappedCubemapImage.getViewCreateInfo(vk::ImageViewType::e2DArray) };

    const vk::raii::DescriptorPool cubemapToneMappingDescriptorPool { gpu.device, getPoolSizes(cubemapToneMappingRenderer.descriptorSetLayout).getDescriptorPoolCreateInfo() };
    const auto [cubemapToneMappingDescriptorSet] = allocateDescriptorSets(*gpu.device, *cubemapToneMappingDescriptorPool, std::tie(cubemapToneMappingRenderer.descriptorSetLayout));
    gpu.device.updateDescriptorSets(
        cubemapToneMappingDescriptorSet.getWriteOne<0>({ {}, *cubemapImageArrayView, vk::ImageLayout::eShaderReadOnlyOptimal }),
        {});

    const vk::raii::Framebuffer cubemapToneMappingFramebuffer { gpu.device, vk::FramebufferCreateInfo {
        {},
        cubemapToneMappingRenderPass,
        *toneMappedCubemapImageArrayView,
        toneMappedCubemapImage.extent.width, toneMappedCubemapImage.extent.height, 1,
    } };

    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
    const vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
    const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };

    const auto [timelineSemaphores, finalWaitValues] = executeHierarchicalCommands(
        gpu.device,
        std::forward_as_tuple(
            // Create device-local eqmap image from staging buffer.
            vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
                eqmapStagingBuffer = std::make_unique<vku::AllocatedBuffer>(vku::MappedBuffer {
                    gpu.allocator,
                    std::from_range, io::StbDecoder<float>::fromFile(eqmapPath.string().c_str(), 4).asSpan(),
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
                vulkan::recordMipmapGenerationCommand(cb, eqmapImage);

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
                vulkan::recordMipmapGenerationCommand(cb, reducedEqmapImage);

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
            // Acquire resources' queue family ownership from compute to graphicsPresent (if necessary), and create tone
            // mapped cubemap image (=toneMappedCubemapImage) from high-precision image (=mippedCubemapGenerator.cubemapImage).
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

                cb.beginRenderPass({
                    *cubemapToneMappingRenderPass,
                    *cubemapToneMappingFramebuffer,
                    vk::Rect2D { { 0, 0 }, vku::toExtent2D(toneMappedCubemapImage.extent) },
                    vku::unsafeProxy<vk::ClearValue>(vk::ClearColorValue{}),
                }, vk::SubpassContents::eInline);

                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *cubemapToneMappingRenderer.pipeline);
                cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *cubemapToneMappingRenderer.pipelineLayout, 0, cubemapToneMappingDescriptorSet, {});
                cb.setViewport(0, vku::unsafeProxy(vku::toViewport(vku::toExtent2D(toneMappedCubemapImage.extent))));
                cb.setScissor(0, vku::unsafeProxy(vk::Rect2D { { 0, 0 }, vku::toExtent2D(toneMappedCubemapImage.extent) }));
                cb.draw(3, 1, 0, 0);

                cb.endRenderPass();
            }, *graphicsCommandPool, gpu.queues.graphicsPresent }));

    const vk::Result semaphoreWaitResult = gpu.device.waitSemaphores({
        {},
        vku::unsafeProxy(timelineSemaphores | ranges::views::deref | std::ranges::to<std::vector>()),
        finalWaitValues
    }, ~0ULL);
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

    if (skyboxResources){
        // Since a descriptor set allocated using ImGui_ImplVulkan_AddTexture cannot be updated, it has to be freed
        // and re-allocated (which done in below).
        (*gpu.device).freeDescriptorSets(*imGuiDescriptorPool, skyboxResources->imGuiEqmapTextureDescriptorSet);
    }

    // Emplace the results into skyboxResources and imageBasedLightingResources.
    vk::raii::ImageView reducedEqmapImageView { gpu.device, reducedEqmapImage.getViewCreateInfo() };
    const vk::DescriptorSet imGuiEqmapImageDescriptorSet
        = static_cast<vk::DescriptorSet>(ImGui_ImplVulkan_AddTexture(
            vku::toCType(*reducedEqmapSampler),
            vku::toCType(*reducedEqmapImageView),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    vk::raii::ImageView toneMappedCubemapImageView { gpu.device, toneMappedCubemapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
    skyboxResources.emplace(
        std::move(reducedEqmapImage),
        std::move(reducedEqmapImageView),
        std::move(toneMappedCubemapImage),
        std::move(toneMappedCubemapImageView),
        imGuiEqmapImageDescriptorSet);

    vk::raii::ImageView prefilteredmapImageView { gpu.device, iblGenerator.prefilteredmapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
    imageBasedLightingResources = {
        std::move(iblGenerator.sphericalHarmonicsBuffer).unmap(),
        std::move(iblGenerator.prefilteredmapImage),
        std::move(prefilteredmapImageView),
    };

    // Update the related descriptor sets.
    gpu.device.updateDescriptorSets({
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<0>({ imageBasedLightingResources.cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<1>({ {}, *imageBasedLightingResources.prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
        sharedData.skyboxDescriptorSet.getWriteOne<0>({ {}, *skyboxResources->cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
    }, {});
}