module;

#include <cassert>

#include <IconsFontAwesome4.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define NOMINMAX // prevent min/max macro redeclaration from <windows.h>
#elifdef __APPLE__
#define GLFW_EXPOSE_NATIVE_COCOA
#elifdef __linux__
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <nfd_glfw3.h>
#ifdef SUPPORT_EXR_SKYBOX
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfThreading.h>
#endif
#include <stb_image.h>
#include <vulkan/vulkan_hpp_macros.hpp>

#if __has_include(<windows.h>)
#undef MemoryBarrier
#endif

module vk_gltf_viewer;
import :MainApp;

import std;
import asset;
import cubemap;
import imgui.glfw;
import imgui.vulkan;
import :gltf.algorithm.misc;
import :gltf.Animation;
import :gltf.AssetExternalBuffers;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.optional;
import :helpers.ranges;
import :helpers.tristate;
import :imgui.TaskCollector;
import :vulkan.Frame;
import :vulkan.generator.ImageBasedLightingResourceGenerator;
import :vulkan.mipmap;
import :vulkan.pipeline.BrdfmapComputer;
import :vulkan.pipeline.CubemapToneMappingRenderer;

#define MOVE_CAP(x) x = std::move(x)
#ifdef _MSC_VER
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

vk_gltf_viewer::MainApp::MainApp() {
    const vulkan::pipeline::BrdfmapComputer brdfmapComputer { gpu.device };

    const vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
    std::variant<vk::CommandPool, vk::raii::CommandPool> graphicsCommandPool = *computeCommandPool;
    if (gpu.queueFamilies.graphicsPresent != gpu.queueFamilies.compute) {
        graphicsCommandPool = decltype(graphicsCommandPool) {
            std::in_place_type<vk::raii::CommandPool>,
            gpu.device,
            vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent },
        };
    }

    const auto [timelineSemaphores, finalWaitValues] = vku::executeHierarchicalCommands(
        gpu.device,
        std::forward_as_tuple(
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
            }, visit_as<vk::CommandPool>(graphicsCommandPool), gpu.queues.graphicsPresent },
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
                brdfmapComputer.compute(
                    cb,
                    vulkan::BrdfmapComputer::DescriptorSetLayout::getWriteOne<0>({ {}, *brdfmapImageView, vk::ImageLayout::eGeneral }),
                    vku::toExtent2D(brdfmapImage.extent));

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
            }, visit_as<vk::CommandPool>(graphicsCommandPool), gpu.queues.graphicsPresent }));

    std::ignore = gpu.device.waitSemaphores({
        {},
        vku::unsafeProxy(timelineSemaphores | ranges::views::deref | std::ranges::to<std::vector>()),
        finalWaitValues
    }, ~0ULL);

    gpu.device.updateDescriptorSets({
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<0>({ imageBasedLightingResources.cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<1>({ {}, *imageBasedLightingResources.prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<2>({ {}, *brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
    }, {});

    const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, visit_as<vk::CommandPool>(graphicsCommandPool), gpu.queues.graphicsPresent, [this](vk::CommandBuffer cb) {
        recordSwapchainImageLayoutTransitionCommands(cb);
    }, *fence);
    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

    // Init ImGui.
    ImGui::CheckVersion();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    const glm::vec2 contentScale = window.getContentScale();
    io.DisplayFramebufferScale = { contentScale.x, contentScale.y };
#if __APPLE__
    io.FontGlobalScale = 1.f / io.DisplayFramebufferScale.x;
#endif
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImFontConfig fontConfig;
    fontConfig.SizePixels = 16.f * io.DisplayFramebufferScale.x;

    const char *defaultFontPath =
#ifdef _WIN32
        "C:\\Windows\\Fonts\\arial.ttf";
#elif __APPLE__
        "/Library/Fonts/Arial Unicode.ttf";
#elif __linux__
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf";
#else
#error "Type your own font file in here!"
#endif
    if (std::filesystem::exists(defaultFontPath)) {
        io.Fonts->AddFontFromFileTTF(defaultFontPath, 16.f * io.DisplayFramebufferScale.x);
    }
    else {
        std::println(std::cerr, "Your system doesn't have expected system font at {}. Low-resolution font will be used instead.", defaultFontPath);
        io.Fonts->AddFontDefault(&fontConfig);
    }

    fontConfig.MergeMode = true;
    constexpr ImWchar fontAwesomeIconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromMemoryCompressedBase85TTF(
        asset::font::fontawesome_webfont_ttf_compressed_data_base85,
        fontConfig.SizePixels, &fontConfig, fontAwesomeIconRanges);

    io.Fonts->Build();

    ImGui_ImplGlfw_InitForVulkan(window, true);
    const vk::Format colorAttachmentFormat = gpu.supportSwapchainMutableFormat ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb;
    ImGui_ImplVulkan_InitInfo initInfo {
        .Instance = *instance,
        .PhysicalDevice = *gpu.physicalDevice,
        .Device = *gpu.device,
        .Queue = gpu.queues.graphicsPresent,
        // ImGui requires ImGui_ImplVulkan_InitInfo::{MinImageCount,ImageCount} ≥ 2 (I don't know why...).
        .MinImageCount = std::max(FRAMES_IN_FLIGHT, 2U),
        .ImageCount = std::max(FRAMES_IN_FLIGHT, 2U),
        .DescriptorPoolSize = 512,
        .UseDynamicRendering = true,
        .PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfo {
            {},
            colorAttachmentFormat,
        },
    };
    ImGui_ImplVulkan_Init(&initInfo);
}

vk_gltf_viewer::MainApp::~MainApp() {
    for (ImTextureID textureDescriptorSet : assetTextureDescriptorSets) {
        ImGui_ImplVulkan_RemoveTexture(*reinterpret_cast<vk::DescriptorSet*>(textureDescriptorSet));
    }
    if (skyboxResources) {
        ImGui_ImplVulkan_RemoveTexture(skyboxResources->imGuiEqmapTextureDescriptorSet);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void vk_gltf_viewer::MainApp::run() {
    // When using multiple frames in flight, updating resources in a frame while it’s still being used by the GPU can
    // lead to data hazards. Since resource updates occur when one of the frames is fenced, that frame can be updated
    // safely, but the others cannot.
    // One way to handle this is by storing update tasks for the other frames and executing them once the target frame
    // becomes idle. This approach is especially efficient for two frames in flight, as it requires only a single task
    // vector to store updates for the “another” frame.
    static_assert(FRAMES_IN_FLIGHT == 2, "Frames ≥ 3 needs different update deferring mechanism.");
    std::vector<std::function<void(vulkan::Frame&)>> deferredFrameUpdateTasks;

    // Booleans that indicates frame at the corresponding index should handle swapchain resizing.
    std::array<bool, FRAMES_IN_FLIGHT> shouldHandleSwapchainResize{};
    std::array<bool, FRAMES_IN_FLIGHT> regenerateDrawCommands{};

    // Currently frame feedback result contains which node is in hovered state, which is only valid
    // with the asset that is used for hovering test. Therefore, if asset may changed, the result is
	// being invalidated. This booleans indicate whether the frame feedback result is valid or not.
    std::array<bool, FRAMES_IN_FLIGHT> frameFeedbackResultValid{};

    // TODO: we need more general mechanism to upload the GPU buffer data in shared data. This is just a stopgap solution
    //  for current KHR_materials_variants implementation.
    const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
    const auto [sharedDataUpdateCommandBuffer] = vku::allocateCommandBuffers<1>(*gpu.device, *graphicsCommandPool);
    bool hasUpdateData = false;

    std::vector<control::Task> tasks;
    for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT) {
        tasks.clear();

        if (gltf) {
            std::vector<std::size_t> transformedNodes, morphedNodes;
            for (const auto &[animation, enabled] : std::views::zip(gltf->animations, gltf->animationEnabled)) {
                if (!enabled) continue;
                animation.update(glfwGetTime(), back_inserter(transformedNodes), back_inserter(morphedNodes), gltf->assetExternalBuffers);
            }

            for (std::size_t nodeIndex : transformedNodes) {
                tasks.emplace_back(std::in_place_type<control::task::ChangeNodeLocalTransform>, nodeIndex);
            }
            for (std::size_t nodeIndex : morphedNodes) {
                const std::size_t targetWeightCount = fastgltf::getTargetWeightCount(gltf->asset.nodes[nodeIndex], gltf->asset);
                tasks.emplace_back(std::in_place_type<control::task::ChangeMorphTargetWeight>, nodeIndex, 0, targetWeightCount);
            }
        }

        // Collect task from window event (mouse, keyboard, drag and drop, ...).
        window.handleEvents(tasks);

        // Collect task from ImGui (button click, menu selection, ...).
        static ImRect passthruRect{};
        {
            ImGui_ImplGlfw_NewFrame();
            ImGui_ImplVulkan_NewFrame();
            control::ImGuiTaskCollector imguiTaskCollector {
                tasks,
                ImVec2 { static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height) },
                passthruRect,
            };

            // Get native window handle.
            nfdwindowhandle_t windowHandle = {};
            NFD_GetNativeWindowFromGLFWWindow(window, &windowHandle);

            imguiTaskCollector.menuBar(appState.getRecentGltfPaths(), appState.getRecentSkyboxPaths(), windowHandle);
            if (auto &gltfAsset = appState.gltfAsset) {
                imguiTaskCollector.assetInspector(gltfAsset->asset, gltf->directory);
                imguiTaskCollector.assetTextures(gltfAsset->asset, assetTextureDescriptorSets, gltf->textureUsage);
                imguiTaskCollector.materialEditor(gltfAsset->asset, assetTextureDescriptorSets);
                if (!gltfAsset->asset.materialVariants.empty()) {
                    imguiTaskCollector.materialVariants(gltfAsset->asset);
                }
                imguiTaskCollector.sceneHierarchy(gltfAsset->asset, gltfAsset->getSceneIndex(), gltfAsset->nodeVisibilities, gltfAsset->hoveringNodeIndex, gltfAsset->selectedNodeIndices);
                imguiTaskCollector.nodeInspector(gltfAsset->asset, gltfAsset->selectedNodeIndices);

                if (!gltfAsset->asset.animations.empty()) {
                    imguiTaskCollector.animations(gltfAsset->asset, gltf->animationEnabled);
                }
            }
            if (const auto &iblInfo = appState.imageBasedLightingProperties) {
                imguiTaskCollector.imageBasedLighting(*iblInfo, vku::toUint64(skyboxResources->imGuiEqmapTextureDescriptorSet));
            }
            imguiTaskCollector.background(appState.canSelectSkyboxBackground, appState.background);
            imguiTaskCollector.inputControl(appState.camera, appState.automaticNearFarPlaneAdjustment, appState.useFrustumCulling, appState.hoveringNodeOutline, appState.selectedNodeOutline);
            if (appState.gltfAsset && appState.gltfAsset->selectedNodeIndices.size() == 1) {
                const std::size_t selectedNodeIndex = *appState.gltfAsset->selectedNodeIndices.begin();
                imguiTaskCollector.imguizmo(appState.camera, gltf->nodeWorldTransforms[selectedNodeIndex], appState.imGuizmoOperation);
            }
            else {
                imguiTaskCollector.imguizmo(appState.camera);
            }
        }

        // Wait for previous frame execution to end.
        vulkan::Frame &frame = frames[frameIndex];
        frame.waitForPreviousExecution();

        for (const auto &task : deferredFrameUpdateTasks) {
            task(frame);
        }
        deferredFrameUpdateTasks.clear();

        for (const control::Task &task : tasks) {
            visit(multilambda {
                [this](const control::task::ChangePassthruRect &task) {
                    appState.camera.aspectRatio = task.newRect.GetWidth() / task.newRect.GetHeight();
                    passthruRect = task.newRect;
                },
                [&](const control::task::LoadGltf &task) {
                    loadGltf(task.path);
                    regenerateDrawCommands.fill(true);
                    frameFeedbackResultValid.fill(false);
                },
                [&](control::task::CloseGltf) {
                    closeGltf();
                },
                [&](const control::task::LoadEqmap &task) {
                    loadEqmap(task.path);
                },
                [&](control::task::ChangeScene task) {
                    gltf->setScene(task.newSceneIndex);

                    auto nodeWorldTransformUpdateTask = [this, sceneIndex = task.newSceneIndex](vulkan::Frame &frame) {
                        frame.gltfAsset->instancedNodeWorldTransformBuffer.update(
                            gltf->asset.scenes[sceneIndex], gltf->nodeWorldTransforms, gltf->assetExternalBuffers);
                    };
                    nodeWorldTransformUpdateTask(frame);
                    deferredFrameUpdateTasks.push_back(std::move(nodeWorldTransformUpdateTask));

                    // Update AppState.
                    appState.gltfAsset->setScene(task.newSceneIndex);

                    // Adjust the camera based on the scene enclosing sphere.
                    const auto &[center, radius] = gltf->sceneMiniball;
                    const float distance = radius / std::sin(appState.camera.fov / 2.f);
                    appState.camera.position = glm::make_vec3(center.data()) - glm::dvec3 { distance * normalize(appState.camera.direction) };
                    appState.camera.zMin = distance - radius;
                    appState.camera.zMax = distance + radius;
                    appState.camera.targetDistance = distance;

                    regenerateDrawCommands.fill(true);
                },
                [this](control::task::ChangeNodeVisibilityType) {
                    appState.gltfAsset->switchNodeVisibilityType();
                },
                [this](control::task::ChangeNodeVisibility task) {
                    visit(multilambda {
                        [&](std::span<std::optional<bool>> visibilities) {
                            if (auto &visibility = visibilities[task.nodeIndex]) {
                                *visibility = !*visibility;
                            }
                            else {
                                visibility.emplace(true);
                            }

                            tristate::propagateTopDown(
                                [&](auto i) -> decltype(auto) { return gltf->asset.nodes[i].children; },
                                task.nodeIndex, visibilities);
                            tristate::propagateBottomUp(
                                [&](auto i) { return gltf->sceneInverseHierarchy.getParentNodeIndex(i).value_or(i); },
                                [&](auto i) -> decltype(auto) { return gltf->asset.nodes[i].children; },
                                task.nodeIndex, visibilities);
                        },
                        [&](std::vector<bool> &visibilities) {
                            visibilities[task.nodeIndex].flip();
                        },
                    }, appState.gltfAsset->nodeVisibilities);
                },
                [this](const control::task::SelectNode &task) {
                    if (!task.combine) {
                        appState.gltfAsset->selectedNodeIndices.clear();
                    }
                    appState.gltfAsset->selectedNodeIndices.emplace(task.nodeIndex);

                    // If selected nodes have a single material, show it in the Material Editor window.
                    if (auto materialIndex = gltf::algorithm::getUniqueMaterialIndex(gltf->asset, appState.gltfAsset->selectedNodeIndices)) {
                        control::ImGuiTaskCollector::selectedMaterialIndex = *materialIndex;;
                    }
                },
                [this](const control::task::HoverNodeFromSceneHierarchy &task) {
                    appState.gltfAsset->hoveringNodeIndex.emplace(task.nodeIndex);
                },
                [&](const control::task::ChangeNodeLocalTransform &task) {
                    fastgltf::math::fmat4x4 baseMatrix { 1.f };
                    if (auto parentNodeIndex = gltf->sceneInverseHierarchy.getParentNodeIndex(task.nodeIndex)) {
                        baseMatrix = gltf->nodeWorldTransforms[*parentNodeIndex];
                    }
                    const fastgltf::math::fmat4x4 nodeWorldTransform = fastgltf::getTransformMatrix(gltf->asset.nodes[task.nodeIndex], baseMatrix);

                    // Update the current and its descendant nodes' world transforms for both host and GPU side data.
                    gltf->nodeWorldTransforms.update(task.nodeIndex, nodeWorldTransform);
                    auto updateNodeTransformTask = [this, nodeIndex = task.nodeIndex](vulkan::Frame &frame) {
                        frame.gltfAsset->instancedNodeWorldTransformBuffer.update(
                            nodeIndex, gltf->nodeWorldTransforms, gltf->assetExternalBuffers);
                    };
                    updateNodeTransformTask(frame);
                    deferredFrameUpdateTasks.push_back(std::move(updateNodeTransformTask));

                    // Scene enclosing sphere would be changed. Adjust the camera's near/far plane if necessary.
                    if (appState.automaticNearFarPlaneAdjustment) {
                        const auto &[center, radius]
                            = gltf->sceneMiniball
                            = gltf::algorithm::getMiniball(gltf->asset, gltf->scene, gltf->nodeWorldTransforms, gltf->assetExternalBuffers);
                        appState.camera.tightenNearFar(glm::make_vec3(center.data()), radius);
                    }
                },
                [&](control::task::ChangeSelectedNodeWorldTransform) {
                    const std::size_t selectedNodeIndex = *appState.gltfAsset->selectedNodeIndices.begin();
                    const fastgltf::math::fmat4x4 &selectedNodeWorldTransform = gltf->nodeWorldTransforms[selectedNodeIndex];

                    // Re-calculate the node local transform.
                    //
                    // glTF specification:
                    // The global transformation matrix of a node is the product of the global transformation matrix of
                    // its parent node and its own local transformation matrix. When the node has no parent node, its
                    // global transformation matrix is identical to its local transformation matrix.
                    //
                    // (node world transform matrix) = (parent node world transform matrix) * (node local transform matrix).
                    // => (node local transform matrix) = (parent node world transform matrix)^-1 * (node world transform matrix).

                    // TODO: replace this function with fastgltf provided if it exported.
                    static constexpr auto affineInverse = []<typename T>(const fastgltf::math::mat<T, 4, 4>& m) noexcept {
                        const auto inv = inverse(fastgltf::math::mat<T, 3, 3>(m));
                        const auto l = -inv * fastgltf::math::vec<T, 3>(m.col(3));
                        return fastgltf::math::mat<T, 4, 4>(
                            fastgltf::math::vec<T, 4>(inv.col(0).x(), inv.col(0).y(), inv.col(0).z(), 0.f),
                            fastgltf::math::vec<T, 4>(inv.col(1).x(), inv.col(1).y(), inv.col(1).z(), 0.f),
                            fastgltf::math::vec<T, 4>(inv.col(2).x(), inv.col(2).y(), inv.col(2).z(), 0.f),
                            fastgltf::math::vec<T, 4>(l.x(), l.y(), l.z(), 1.f));
                    };

                    visit(fastgltf::visitor {
                        [&](fastgltf::math::fmat4x4 &transformMatrix) {
                            if (auto parentNodeIndex = gltf->sceneInverseHierarchy.getParentNodeIndex(selectedNodeIndex)) {
                                transformMatrix = affineInverse(gltf->nodeWorldTransforms[*parentNodeIndex]) * selectedNodeWorldTransform;
                            }
                            else {
                                transformMatrix = selectedNodeWorldTransform;
                            }
                        },
                        [&](fastgltf::TRS &trs) {
                            if (auto parentNodeIndex = gltf->sceneInverseHierarchy.getParentNodeIndex(selectedNodeIndex)) {
                                const fastgltf::math::fmat4x4 transformMatrix = affineInverse(gltf->nodeWorldTransforms[*parentNodeIndex]) * selectedNodeWorldTransform;
                                decomposeTransformMatrix(transformMatrix, trs.scale, trs.rotation, trs.translation);
                            }
                            else {
                                decomposeTransformMatrix(selectedNodeWorldTransform, trs.scale, trs.rotation, trs.translation);
                            }
                        },
                    }, gltf->asset.nodes[selectedNodeIndex].transform);

                    // Update the current and its descendant nodes' world transforms for both host and GPU side data.
                    gltf->nodeWorldTransforms.update(selectedNodeIndex, selectedNodeWorldTransform);
                    auto updateNodeTransformTask = [this, selectedNodeIndex](vulkan::Frame &frame) {
                        frame.gltfAsset->instancedNodeWorldTransformBuffer.update(
                            selectedNodeIndex, gltf->nodeWorldTransforms, gltf->assetExternalBuffers);
                    };
                    updateNodeTransformTask(frame);
                    deferredFrameUpdateTasks.push_back(std::move(updateNodeTransformTask));

                    // Scene enclosing sphere would be changed. Adjust the camera's near/far plane if necessary.
                    if (appState.automaticNearFarPlaneAdjustment) {
                        const auto &[center, radius]
                            = gltf->sceneMiniball
                            = gltf::algorithm::getMiniball(gltf->asset, gltf->scene, gltf->nodeWorldTransforms, gltf->assetExternalBuffers);
                        appState.camera.tightenNearFar(glm::make_vec3(center.data()), radius);
                    }
                },
                [this](control::task::TightenNearFarPlane) {
                    if (gltf) {
                        const auto &[center, radius] = gltf->sceneMiniball;
                        appState.camera.tightenNearFar(glm::make_vec3(center.data()), radius);
                    }
                },
                [this](control::task::ChangeCameraView) {
                    if (appState.automaticNearFarPlaneAdjustment && gltf) {
                        // Tighten near/far plane based on the scene enclosing sphere.
                        const auto &[center, radius] = gltf->sceneMiniball;
                        appState.camera.tightenNearFar(glm::make_vec3(center.data()), radius);
                    }
                },
                [&](control::task::InvalidateDrawCommandSeparation) {
                    regenerateDrawCommands.fill(true);
                },
                [&](const control::task::SelectMaterialVariants &task) {
                    assert(gltf && "Synchronization error: gltf is unset but material variants are selected.");

                    gpu.device.waitIdle();

                    graphicsCommandPool.reset();
                    sharedDataUpdateCommandBuffer.begin(vk::CommandBufferBeginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

                    for (const auto &[pPrimitive, materialIndex] : gltf->materialVariantsMapping.at(task.variantIndex)) {
                        pPrimitive->materialIndex.emplace(materialIndex);
                        hasUpdateData |= sharedData.gltfAsset->primitiveBuffer.updateMaterial(
                            gltf->orderedPrimitives.getIndex(*pPrimitive), static_cast<std::uint32_t>(materialIndex), sharedDataUpdateCommandBuffer);
                    }

                    sharedDataUpdateCommandBuffer.end();

                    if (hasUpdateData) {
                        gpu.queues.graphicsPresent.submit(vk::SubmitInfo { {}, {}, sharedDataUpdateCommandBuffer });
                        gpu.device.waitIdle();
                    }
                },
                [&](const control::task::ChangeMorphTargetWeight &task) {
                    auto updateTargetWeightTask = [this, task](vulkan::Frame &frame) {
                        const std::span targetWeights = getTargetWeights(gltf->asset.nodes[task.nodeIndex], gltf->asset);
                        for (auto weightIndex = task.targetWeightStartIndex; float weight : targetWeights.subspan(task.targetWeightStartIndex, task.targetWeightCount)) {
                            frame.gltfAsset->morphTargetWeightBuffer.updateWeight(task.nodeIndex, weightIndex++, weight);
                        }
                    };
                    updateTargetWeightTask(frame);
                    deferredFrameUpdateTasks.push_back(std::move(updateTargetWeightTask));

                    // Scene enclosing sphere would be changed. Adjust the camera's near/far plane if necessary.
                    if (appState.automaticNearFarPlaneAdjustment) {
                        const auto &[center, radius]
                            = gltf->sceneMiniball
                            = gltf::algorithm::getMiniball(gltf->asset, gltf->scene, gltf->nodeWorldTransforms, gltf->assetExternalBuffers);
                        appState.camera.tightenNearFar(glm::make_vec3(center.data()), radius);
                    }
                },
            }, task);
        }

        // Update frame resources.
        const vulkan::Frame::UpdateResult updateResult = frame.update({
            .passthruRect = vk::Rect2D {
                { static_cast<std::int32_t>(passthruRect.Min.x), static_cast<std::int32_t>(passthruRect.Min.y) },
                { static_cast<std::uint32_t>(passthruRect.GetWidth()), static_cast<std::uint32_t>(passthruRect.GetHeight()) },
            },
            .camera = { appState.camera.getViewMatrix(), appState.camera.getProjectionMatrix() },
            .frustum = value_if(appState.useFrustumCulling, [this]() {
                return appState.camera.getFrustum();
            }),
            .cursorPosFromPassthruRectTopLeft = appState.hoveringMousePosition.and_then([&](const glm::vec2 &position) -> std::optional<vk::Offset2D> {
                // If cursor is outside the framebuffer, cursor position is undefined.
                const glm::vec2 framebufferSize = window.getFramebufferSize();
                const glm::vec2 framebufferCursorPosition = position * framebufferSize / glm::vec2 { window.getSize() };
                if (framebufferCursorPosition.x >= framebufferSize.x || framebufferCursorPosition.y >= framebufferSize.y) return std::nullopt;

                const vk::Offset2D offset {
                    static_cast<std::int32_t>(framebufferCursorPosition.x - passthruRect.Min.x),
                    static_cast<std::int32_t>(framebufferCursorPosition.y - passthruRect.Min.y),
                };
                return value_if(0 <= offset.x && offset.x < passthruRect.GetWidth() && 0 <= offset.y && offset.y < passthruRect.GetHeight(), offset);
            }),
            .gltf = gltf.transform([&](Gltf &gltf) {
                assert(appState.gltfAsset && "Synchronization error: gltfAsset is not set in AppState.");
                return vulkan::Frame::ExecutionTask::Gltf {
                    .asset = gltf.asset,
                    .orderedPrimitives = gltf.orderedPrimitives,
                    .nodeWorldTransforms = gltf.nodeWorldTransforms,
                    .regenerateDrawCommands = std::exchange(regenerateDrawCommands[frameIndex], false),
                    .renderingNodes = {
                        .indices = appState.gltfAsset->getVisibleNodeIndices(),
                    },
                    .hoveringNode = transform([&](std::uint16_t index, const AppState::Outline &outline) {
                        return vulkan::Frame::ExecutionTask::Gltf::HoveringNode {
                            index, outline.color, outline.thickness,
                        };
                    }, appState.gltfAsset->hoveringNodeIndex, appState.hoveringNodeOutline.to_optional()),
                    .selectedNodes = value_if(!appState.gltfAsset->selectedNodeIndices.empty() && appState.selectedNodeOutline.has_value(), [&]() {
                        return vulkan::Frame::ExecutionTask::Gltf::SelectedNodes {
                            appState.gltfAsset->selectedNodeIndices,
                            appState.selectedNodeOutline->color,
                            appState.selectedNodeOutline->thickness,
                        };
                    }),
                };
            }),
            .solidBackground = appState.background.to_optional(),
            .handleSwapchainResize = std::exchange(shouldHandleSwapchainResize[frameIndex], false),
        });

		if (frameFeedbackResultValid[frameIndex]) {
            // Feedback the update result into this.
            if (appState.gltfAsset) {
                appState.gltfAsset->hoveringNodeIndex = updateResult.hoveringNodeIndex;
            }
		}
        else {
			frameFeedbackResultValid[frameIndex] = true;
        }

        try {
            // Acquire the next swapchain image.
            // Note: ignoring vk::Result is okay because it would be handled by the outer try-catch block.
            const std::uint32_t swapchainImageIndex = (*gpu.device).acquireNextImageKHR(
                *swapchain, ~0ULL, frame.getSwapchainImageAcquireSemaphore()).value;

            // Execute frame.
            frame.recordCommandsAndSubmit(swapchainImageIndex);

            // Present the rendered swapchain image to swapchain.
            if (gpu.queues.graphicsPresent.presentKHR({
                vku::unsafeProxy(frame.getSwapchainImageReadySemaphore()),
                *swapchain,
                swapchainImageIndex,
            }) == vk::Result::eSuboptimalKHR) {
                // The result codes VK_ERROR_OUT_OF_DATE_KHR and VK_SUBOPTIMAL_KHR have the same meaning when
                // returned by vkQueuePresentKHR as they do when returned by vkAcquireNextImageKHR.
                throw vk::OutOfDateKHRError { "Suboptimal swapchain" };
            }
        }
        catch (const vk::OutOfDateKHRError&) {
            gpu.device.waitIdle();

            // Make process idle state if window is minimized.
            while (!glfwWindowShouldClose(window) && (swapchainExtent = getSwapchainExtent()) == vk::Extent2D{}) {
                std::this_thread::yield();
            }

            // Update swapchain.
            swapchain = createSwapchain(*swapchain);
            swapchainImages = swapchain.getImages();

            // Change swapchain image layout.
            const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
            const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
            vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
                recordSwapchainImageLayoutTransitionCommands(cb);
            }, *fence);
            std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

            // Update frame shared data and frames.
            sharedData.handleSwapchainResize(swapchainExtent, swapchainImages);
            shouldHandleSwapchainResize.fill(true);
        }
    }
    gpu.device.waitIdle();
}

vk_gltf_viewer::MainApp::Gltf::Gltf(fastgltf::Parser &parser, const std::filesystem::path &path)
    : dataBuffer { get_checked(fastgltf::GltfDataBuffer::FromPath(path)) }
    , directory { path.parent_path() }
    , asset { get_checked(parser.loadGltf(dataBuffer, directory)) }
    , orderedPrimitives { asset }
    , animations { std::from_range, asset.animations | std::views::transform([&](const fastgltf::Animation &animation) {
        return gltf::Animation { asset, animation, assetExternalBuffers };
    }) }
    , animationEnabled { std::vector(asset.animations.size(), false) }
    , nodeWorldTransforms { asset, scene }
    , sceneInverseHierarchy { asset, scene } {
    sceneMiniball = gltf::algorithm::getMiniball(asset, scene, nodeWorldTransforms, assetExternalBuffers);
}

void vk_gltf_viewer::MainApp::Gltf::setScene(std::size_t sceneIndex) {
    scene = asset.scenes[sceneIndex];
    nodeWorldTransforms.update(scene);
    sceneInverseHierarchy = { asset, scene };
    sceneMiniball = gltf::algorithm::getMiniball(asset, scene, nodeWorldTransforms, assetExternalBuffers);
}

vk::raii::Instance vk_gltf_viewer::MainApp::createInstance() const {
    std::vector<const char*> extensions{
#if __APPLE__
        vk::KHRPortabilityEnumerationExtensionName,
#endif
    };

    std::uint32_t glfwExtensionCount;
    const auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    extensions.append_range(std::views::counted(glfwExtensions, glfwExtensionCount));

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
        extensions,
    } };
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
    return instance;
}

vk::raii::SwapchainKHR vk_gltf_viewer::MainApp::createSwapchain(vk::SwapchainKHR oldSwapchain) const {
    const vk::SurfaceKHR surface = window.getSurface();
    const vk::SurfaceCapabilitiesKHR surfaceCapabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);
    const auto viewFormats = { vk::Format::eB8G8R8A8Srgb, vk::Format::eB8G8R8A8Unorm };

    vk::StructureChain createInfo {
        vk::SwapchainCreateInfoKHR{
            gpu.supportSwapchainMutableFormat ? vk::SwapchainCreateFlagBitsKHR::eMutableFormat : vk::SwapchainCreateFlagsKHR{},
            surface,
            // The spec says:
            //
            //   maxImageCount is the maximum number of images the specified device supports for a swapchain created for
            //   the surface, and will be either 0, or greater than or equal to minImageCount. A value of 0 means that
            //   there is no limit on the number of images, though there may be limits related to the total amount of
            //   memory used by presentable images.
            //
            // Therefore, if maxImageCount is zero, it is set to the UINT_MAX and minImageCount + 1 will be used.
            std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount == 0 ? ~0U : surfaceCapabilities.maxImageCount),
            vk::Format::eB8G8R8A8Srgb,
            vk::ColorSpaceKHR::eSrgbNonlinear,
            swapchainExtent,
            1,
            vk::ImageUsageFlagBits::eColorAttachment,
            {}, {},
            surfaceCapabilities.currentTransform,
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::PresentModeKHR::eFifo,
            true,
            oldSwapchain,
        },
        vk::ImageFormatListCreateInfo {
            viewFormats,
        }
    };
    if (!gpu.supportSwapchainMutableFormat) {
        createInfo.unlink<vk::ImageFormatListCreateInfo>();
    }

    return { gpu.device, createInfo.get() };
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

void vk_gltf_viewer::MainApp::loadGltf(const std::filesystem::path &path) {
    try {
        const Gltf &inner = gltf.emplace(parser, path);

        // TODO: I'm aware that there are better solutions compare to the waitIdle, but I don't have much time for it
        //  so I'll just use it for now.
        gpu.device.waitIdle();
        sharedData.changeAsset(inner.asset, path.parent_path(), inner.orderedPrimitives, inner.assetExternalBuffers);
        for (vulkan::Frame &frame : frames) {
            frame.changeAsset(inner.asset, inner.nodeWorldTransforms, inner.assetExternalBuffers);
        }
    }
    catch (gltf::AssetProcessError error) {
        std::println(std::cerr, "The glTF file cannot be processed because of an error: {}", to_string(error));
        closeGltf();
        return;
    }
    catch (fastgltf::Error error) {
        // If error is due to missing or unknown required extension, show a message and return.
        if (ranges::one_of(error, fastgltf::Error::MissingExtensions, fastgltf::Error::UnknownRequiredExtension)) {
            std::println(std::cerr, "The glTF file requires an extension that is not supported by this application.");
            closeGltf();
            return;
        }
        else {
            // Application fault.
            std::rethrow_exception(std::current_exception());
        }
    }

    // TODO: due to the ImGui's gamma correction issue, base color/emissive texture is rendered darker than it should be.
    assetTextureDescriptorSets
        = sharedData.gltfAsset->textures.descriptorInfos
        | std::views::transform([](const vk::DescriptorImageInfo &descriptorInfo) {
            return reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(
                descriptorInfo.sampler, descriptorInfo.imageView, static_cast<VkImageLayout>(descriptorInfo.imageLayout)));
        })
        | std::ranges::to<std::vector>();

    // Change window title.
    window.setTitle(PATH_C_STR(path.filename()));

    // Update AppState.
    appState.gltfAsset.emplace(gltf->asset);
    appState.pushRecentGltfPath(path);

    // Adjust the camera based on the scene enclosing sphere.
    const auto &[center, radius] = gltf->sceneMiniball;
    const float distance = radius / std::sin(appState.camera.fov / 2.f);
    appState.camera.position = glm::make_vec3(center.data()) - glm::dvec3 { distance * normalize(appState.camera.direction) };
    appState.camera.zMin = distance - radius;
    appState.camera.zMax = distance + radius;
    appState.camera.targetDistance = distance;

    control::ImGuiTaskCollector::selectedMaterialIndex.reset();
}

void vk_gltf_viewer::MainApp::closeGltf() {
    gltf.reset();

    gpu.device.waitIdle();
    appState.gltfAsset.reset();

    window.setTitle("Vulkan glTF Viewer");
}

void vk_gltf_viewer::MainApp::loadEqmap(const std::filesystem::path &eqmapPath) {
    const auto [eqmapImageExtent, eqmapStagingBuffer] = [&]() {
        if (auto extension = eqmapPath.extension(); extension == ".hdr") {
            int width, height;
            std::unique_ptr<float[]> data; // It should be freed after copying to the staging buffer, therefore declared as unique_ptr.
            data.reset(stbi_loadf(PATH_C_STR(eqmapPath), &width, &height, nullptr, 4));
            if (!data) {
                throw std::runtime_error { std::format("Failed to load image: {}", stbi_failure_reason()) };
            }

            return std::pair {
                vk::Extent2D { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) },
                vku::MappedBuffer {
                    gpu.allocator,
                    std::from_range, std::span { data.get(), static_cast<std::size_t>(4 * width * height) },
                    vk::BufferUsageFlagBits::eTransferSrc,
                },
            };
        } // After this scope, data will be automatically freed.
#ifdef SUPPORT_EXR_SKYBOX
        else if (extension == ".exr") {
            Imf::InputFile file{ PATH_C_STR(eqmapPath), static_cast<int>(std::thread::hardware_concurrency()) };

            const Imath::Box2i dw = file.header().dataWindow();
            const vk::Extent2D eqmapExtent{
                static_cast<std::uint32_t>(dw.max.x - dw.min.x + 1),
                static_cast<std::uint32_t>(dw.max.y - dw.min.y + 1),
            };

            vku::MappedBuffer buffer{ gpu.allocator, vk::BufferCreateInfo {
                {},
                blockSize(vk::Format::eR32G32B32A32Sfloat) * eqmapExtent.width * eqmapExtent.height,
                vk::BufferUsageFlagBits::eTransferSrc,
            } };
            const std::span data = buffer.asRange<glm::vec4>();

            // Create frame buffers for each channel.
            // Note: Alpha channel will be ignored.
            Imf::FrameBuffer frameBuffer;
            const std::size_t rowBytes = eqmapExtent.width * sizeof(glm::vec4);
            frameBuffer.insert("R", Imf::Slice{ Imf::FLOAT, reinterpret_cast<char*>(&data[0].x), sizeof(glm::vec4), rowBytes });
            frameBuffer.insert("G", Imf::Slice{ Imf::FLOAT, reinterpret_cast<char*>(&data[0].y), sizeof(glm::vec4), rowBytes });
            frameBuffer.insert("B", Imf::Slice{ Imf::FLOAT, reinterpret_cast<char*>(&data[0].z), sizeof(glm::vec4), rowBytes });

            file.readPixels(frameBuffer, dw.min.y, dw.max.y);

            return std::pair{ eqmapExtent, std::move(buffer) };
        }

        throw std::runtime_error { "Unknown file format: only HDR and EXR are supported." };
#else
		throw std::runtime_error{ "Unknown file format: only HDR is supported." };
#endif

    }();

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

    const vk::Extent3D reducedEqmapImageExtent = eqmapImage.mipExtent(eqmapImage.mipLevels - 1);
    vku::AllocatedImage reducedEqmapImage { gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eB10G11R11UfloatPack32,
        reducedEqmapImageExtent,
        vku::Image::maxMipLevels(reducedEqmapImageExtent), 1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
    } };

    vku::AllocatedImage cubemapImage { gpu.allocator, vk::ImageCreateInfo {
        vk::ImageCreateFlagBits::eCubeCompatible,
        vk::ImageType::e2D,
        vk::Format::eR32G32B32A32Sfloat,
        vk::Extent3D { 1024, 1024, 1 },
        vku::Image::maxMipLevels(1024), 6,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        cubemap::CubemapComputer::requiredCubemapImageUsageFlags
            | cubemap::SubgroupMipmapComputer::requiredImageUsageFlags
            | vk::ImageUsageFlagBits::eSampled,
    } };

    const vk::raii::Sampler eqmapSampler { gpu.device, vk::SamplerCreateInfo { {}, vk::Filter::eLinear, vk::Filter::eLinear }.setMaxLod(vk::LodClampNone) };

    const cubemap::CubemapComputer cubemapComputer { gpu.device, eqmapImage, eqmapSampler, cubemapImage };
    const cubemap::SubgroupMipmapComputer subgroupMipmapComputer { gpu.device, cubemapImage, {
        .subgroupSize = gpu.subgroupSize,
        .useShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod,
    } };

    // Generate IBL resources.
    constexpr vulkan::ImageBasedLightingResourceGenerator::Config iblGeneratorConfig {
        .prefilteredmapImageUsage = vk::ImageUsageFlagBits::eSampled,
        .sphericalHarmonicsBufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    };
    vulkan::ImageBasedLightingResourceGenerator iblGenerator { gpu, iblGeneratorConfig };
    const vulkan::ImageBasedLightingResourceGenerator::Pipelines iblGeneratorPipelines {
        vulkan::pipeline::PrefilteredmapComputer { gpu, { vku::Image::maxMipLevels(iblGeneratorConfig.prefilteredmapSize), 1024 } },
        vulkan::pipeline::SphericalHarmonicsComputer { gpu.device },
        vulkan::pipeline::SphericalHarmonicCoefficientsSumComputer { gpu.device },
        vulkan::pipeline::MultiplyComputer { gpu.device },
    };

    // Generate Tone-mapped cubemap.
    const vulkan::rp::CubemapToneMapping cubemapToneMappingRenderPass { gpu.device };
    const vulkan::CubemapToneMappingRenderer cubemapToneMappingRenderer { gpu.device, cubemapToneMappingRenderPass };

    vku::AllocatedImage toneMappedCubemapImage { gpu.allocator, vk::ImageCreateInfo {
        vk::ImageCreateFlagBits::eCubeCompatible,
        vk::ImageType::e2D,
        vk::Format::eB8G8R8A8Srgb,
        cubemapImage.extent,
        1, cubemapImage.arrayLayers,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
    } };
    const vk::raii::ImageView cubemapImageArrayView {
        gpu.device,
        cubemapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 }, vk::ImageViewType::e2DArray),
    };
    const vk::raii::ImageView toneMappedCubemapImageArrayView { gpu.device, toneMappedCubemapImage.getViewCreateInfo(vk::ImageViewType::e2DArray) };

    const vk::raii::Framebuffer cubemapToneMappingFramebuffer { gpu.device, vk::FramebufferCreateInfo {
        {},
        cubemapToneMappingRenderPass,
        *toneMappedCubemapImageArrayView,
        toneMappedCubemapImage.extent.width, toneMappedCubemapImage.extent.height, 1,
    } };

    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };

    std::variant<vk::CommandPool, vk::raii::CommandPool> computeCommandPool = *transferCommandPool;
    if (gpu.queueFamilies.compute != gpu.queueFamilies.transfer) {
        computeCommandPool = decltype(computeCommandPool) {
            std::in_place_type<vk::raii::CommandPool>,
            gpu.device,
            vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute },
        };
    }

    std::variant<vk::CommandPool, vk::raii::CommandPool> graphicsCommandPool = *transferCommandPool;
    if (gpu.queueFamilies.graphicsPresent != gpu.queueFamilies.transfer) {
        graphicsCommandPool = decltype(graphicsCommandPool) {
            std::in_place_type<vk::raii::CommandPool>,
            gpu.device,
            vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent },
        };
    }

    const auto [timelineSemaphores, finalWaitValues] = executeHierarchicalCommands(
        gpu.device,
        std::forward_as_tuple(
            // Create device-local eqmap image from staging buffer.
            vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
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
                    eqmapStagingBuffer,
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
                        { vk::Offset3D{}, vku::toOffset3D(eqmapImage.mipExtent(eqmapImage.mipLevels - 1)) },
                        { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                        { vk::Offset3D{}, vku::toOffset3D(reducedEqmapImage.extent) },
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
            }, visit_as<vk::CommandPool>(graphicsCommandPool), gpu.queues.graphicsPresent }),
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
            }, visit_as<vk::CommandPool>(graphicsCommandPool), gpu.queues.graphicsPresent/*, 4*/ },
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

                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                    {}, {}, {},
                    vk::ImageMemoryBarrier {
                        {}, vk::AccessFlagBits::eShaderWrite,
                        {}, vk::ImageLayout::eGeneral,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        cubemapImage, vku::fullSubresourceRange(),
                    });

                // Generate cubemap from eqmapImage.
                cubemapComputer.recordCommands(cb);

                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                    {},
                    // Ensure eqmap to cubemap projection finish before generating mipmaps.
                    vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                    {}, {});

                // Generate cubemapImage mipmaps.
                subgroupMipmapComputer.recordCommands(cb);

                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                    {}, {}, {},
                    vk::ImageMemoryBarrier {
                        vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                        vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        cubemapImage, vku::fullSubresourceRange(),
                    });

                iblGenerator.recordCommands(cb, iblGeneratorPipelines, cubemapImage);

                // Cubemap and prefilteredmap will be used as sampled image.
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eBottomOfPipe,
                    {}, {}, {},
                    {
                        vk::ImageMemoryBarrier {
                            vk::AccessFlagBits::eShaderWrite, {},
                            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                            gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
                            cubemapImage, vku::fullSubresourceRange(),
                        },
                        vk::ImageMemoryBarrier {
                            vk::AccessFlagBits::eShaderWrite, {},
                            vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                            gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
                            iblGenerator.prefilteredmapImage, vku::fullSubresourceRange(),
                        },
                    });
            }, visit_as<vk::CommandPool>(computeCommandPool), gpu.queues.compute }),
        std::forward_as_tuple(
            // Acquire resources' queue family ownership from compute to graphicsPresent (if necessary), and create tone
            // mapped cubemap image (=toneMappedCubemapImage) from high-precision image (=cubemapImage).
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
                                cubemapImage, vku::fullSubresourceRange(),
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
                cb.pushDescriptorSetKHR(
                    vk::PipelineBindPoint::eGraphics,
                    *cubemapToneMappingRenderer.pipelineLayout,
                    0, vulkan::CubemapToneMappingRenderer::DescriptorSetLayout::getWriteOne<0>({ {}, *cubemapImageArrayView, vk::ImageLayout::eShaderReadOnlyOptimal }));
                cb.setViewport(0, vku::unsafeProxy(vku::toViewport(vku::toExtent2D(toneMappedCubemapImage.extent))));
                cb.setScissor(0, vku::unsafeProxy(vk::Rect2D { { 0, 0 }, vku::toExtent2D(toneMappedCubemapImage.extent) }));
                cb.draw(3, 1, 0, 0);

                cb.endRenderPass();
            }, visit_as<vk::CommandPool>(graphicsCommandPool), gpu.queues.graphicsPresent }));

    std::ignore = gpu.device.waitSemaphores({
        {},
        vku::unsafeProxy(timelineSemaphores | ranges::views::deref | std::ranges::to<std::vector>()),
        finalWaitValues
    }, ~0ULL);

    // Update AppState.
    appState.canSelectSkyboxBackground = true;
    appState.background.reset();
    appState.imageBasedLightingProperties = AppState::ImageBasedLighting {
        .eqmap = {
            .path = eqmapPath,
            .dimension = { eqmapImage.extent.width, eqmapImage.extent.height },
        },
        .cubemap = {
            .size = 1024,
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
        ImGui_ImplVulkan_RemoveTexture(skyboxResources->imGuiEqmapTextureDescriptorSet);
    }

    // Emplace the results into skyboxResources and imageBasedLightingResources.
    vk::raii::ImageView reducedEqmapImageView { gpu.device, reducedEqmapImage.getViewCreateInfo() };
    const vk::DescriptorSet imGuiEqmapImageDescriptorSet = ImGui_ImplVulkan_AddTexture(
        *reducedEqmapSampler,
        *reducedEqmapImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

    // Update AppState.
    appState.pushRecentSkyboxPath(eqmapPath);
}

void vk_gltf_viewer::MainApp::recordSwapchainImageLayoutTransitionCommands(vk::CommandBuffer cb) const {
    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, {}, {},
        swapchainImages
        | std::views::transform([](vk::Image swapchainImage) {
            return vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::ePresentSrcKHR,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                swapchainImage, vku::fullSubresourceRange(),
            };
        })
        | std::ranges::to<std::vector>());
}
