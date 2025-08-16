module;

#include <cassert>
#if __clang__
#include <filesystem>
#endif

#include <GLFW/glfw3.h>
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

#ifdef _WIN32
#define DEFAULT_FONT_PATH "C:\\Windows\\Fonts\\arial.ttf"
#elif __APPLE__
#define DEFAULT_FONT_PATH "/System/Library/Fonts/SFNS.ttf"
#elif __linux__
#define DEFAULT_FONT_PATH "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf"
#else
#error "Type your own font file in here!"
#endif

module vk_gltf_viewer.MainApp;

import cubemap;
import ibl;
import imgui.glfw;
import imgui.vulkan;

import vk_gltf_viewer.asset;
import vk_gltf_viewer.global;
import vk_gltf_viewer.gltf.algorithm.miniball;
import vk_gltf_viewer.gui.popup;
import vk_gltf_viewer.helpers.concepts;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.optional;
import vk_gltf_viewer.helpers.ranges;
import vk_gltf_viewer.imgui.TaskCollector;
import vk_gltf_viewer.math.extended_arithmetic;
import vk_gltf_viewer.vulkan.FrameDeferredTask;
import vk_gltf_viewer.vulkan.imgui.PlatformResource;
import vk_gltf_viewer.vulkan.mipmap;
import vk_gltf_viewer.vulkan.pipeline.CubemapToneMappingRenderPipeline;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }
#define INDEX_SEQ(Is, N, ...) [&]<auto ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })
#ifdef _WIN32
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

template <typename T, glm::qualifier Q>
[[nodiscard]] constexpr ImVec2 toImVec2(const glm::vec<2, T, Q> &v) noexcept {
    return { static_cast<float>(v.x), static_cast<float>(v.y) };
}

[[nodiscard]] glm::mat3x2 getTextureTransform(const fastgltf::TextureTransform *transform) noexcept {
    if (transform) {
        const float c = std::cos(transform->rotation), s = std::sin(transform->rotation);
        return { // Note: column major. A row in code actually means a column in the matrix.
            transform->uvScale[0] * c, transform->uvScale[0] * -s,
            transform->uvScale[1] * s, transform->uvScale[1] * c,
            transform->uvOffset[0], transform->uvOffset[1],
        };
    }
    else {
        return { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
    }
}

vk_gltf_viewer::MainApp::MainApp()
    : instance { createInstance() }
    , window { instance }
    , drawSelectionRectangle { false }
    , lastMouseEnteredViewIndex { 0 }
    , gpu { instance, window.getSurface() }
    , renderer { std::make_shared<Renderer>(Renderer::Capabilities {
        .perFragmentBloom = gpu.supportShaderStencilExport,
    }) }
    , swapchain { gpu, window.getSurface(), getSwapchainExtent() }
    , sharedData { gpu, swapchain.extent, swapchain.images }
    , frames { ARRAY_OF(2, vulkan::Frame { renderer, sharedData }) } {
    const ibl::BrdfmapRenderPipeline brdfmapRenderPipeline { gpu.device, brdfmapImage, {} };
    const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
    const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
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

        // Change brdfmapImage layout to GENERAL.
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                {}, vk::AccessFlagBits::eColorAttachmentWrite,
                {}, vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                brdfmapImage, vku::fullSubresourceRange(),
            });

        // Compute BRDF.
        brdfmapRenderPipeline.recordCommands(cb);

        // brdfmapImage will be used as sampled image.
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eColorAttachmentWrite, {},
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                brdfmapImage, vku::fullSubresourceRange(),
            });

        recordSwapchainImageLayoutTransitionCommands(cb);
    }, *fence);

    gpu.device.updateDescriptorSets({
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<0>({ imageBasedLightingResources.cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<1>({ {}, *imageBasedLightingResources.prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<2>({ {}, *brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
    }, {});

    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL);
}

void vk_gltf_viewer::MainApp::run() {
    // When using multiple frames in flight, updating resources in a frame while it’s still being used by the GPU can
    // lead to data hazards. Since resource updates occur when one of the frames is fenced, that frame can be updated
    // safely, but the others cannot.
    // One way to handle this is by storing update tasks for the other frames and executing them once the target frame
    // becomes idle. This approach is especially efficient for two frames in flight, as it requires only a single task
    // vector to store updates for the “another” frame.
    vulkan::FrameDeferredTask frameDeferredTask;

    std::array<bool, FRAMES_IN_FLIGHT> regenerateDrawCommands{};

    // Current application running loop flow is like:
    //
    // while (application is running) {
    //    waitForPreviousFrameGpuExecution();
    //
    //    // Collect frame tasks.
    //    pollEvents(); // <- Collect task from window events (mouse, keyboard, drag and drop, ...).
    //    imGuiFuncs(); // <- Collect task from GUI input (buton click, ...).
    //
    //    processFrameTasks();
    //
    //    recordCommandBuffers(); // Will call ImGui_ImplVulkan_RenderDrawData().
    //    submitCommandBuffers();
    //    presentFrame();
    // }
    //
    // The problem is: changing glTF asset may be requested via ImGui menu click, and the previous asset will be
    // destroyed in processFrameTasks() function. But, imGuiFuncs() was already executed, and it will store the
    // ImTextureID (=VkDescriptorSet) in its internal state. Then recordCommandBuffers() will try to use the already
    // destroyed texture.
    //
    // To work-around, before destroying the previously loaded asset, its ownership is shared to
    // retainedAssetExtended[frameIndex % FRAMES_IN_FLIGHT], to prevent the asset is completely destroyed. The asset
    // will be retained during its frame execution, and should be destroyed after the frame execution is completed.
    std::array<std::shared_ptr<const gltf::AssetExtended>, FRAMES_IN_FLIGHT> retainedAssetExtended;

    // TODO: we need more general mechanism to upload the GPU buffer data in shared data. This is just a stopgap solution
    //  for current KHR_materials_variants implementation.
    vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
    auto [sharedDataUpdateCommandBuffer] = vku::allocateCommandBuffers<1>(*gpu.device, *graphicsCommandPool);
    std::vector<vku::AllocatedBuffer> sharedDataStagingBuffers;

    for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); ++frameIndex) {
        bool hasUpdateData = false;

        std::queue<control::Task> tasks;

        std::vector<std::size_t> transformedNodes;

        // Collect task from animation system.
        if (assetExtended) {
            std::vector<std::size_t> morphedNodes;
            for (const auto &[animation, enabled] : assetExtended->animations) {
                if (!enabled) continue;
                animation.update(glfwGetTime(), transformedNodes, morphedNodes, assetExtended->externalBuffers);
            }

            for (std::size_t nodeIndex : morphedNodes) {
                const std::size_t targetWeightCount = getTargetWeightCount(assetExtended->asset.nodes[nodeIndex], assetExtended->asset);
                tasks.emplace(std::in_place_type<control::task::MorphTargetWeightChanged>, nodeIndex, 0, targetWeightCount);
            }
        }

        // Collect task from window event (mouse, keyboard, drag and drop, ...).
        window.pollEvents(tasks);

        // Collect task from ImGui (button click, menu selection, ...).
        static ImRect passthruRect{};
        {
            ImGui_ImplGlfw_NewFrame();
            ImGui_ImplVulkan_NewFrame();
            control::ImGuiTaskCollector imguiTaskCollector { tasks, passthruRect };

            // Get native window handle.
            nfdwindowhandle_t windowHandle = {};
            NFD_GetNativeWindowFromGLFWWindow(window, &windowHandle);

            imguiTaskCollector.menuBar(appState.getRecentGltfPaths(), appState.getRecentSkyboxPaths(), windowHandle);
            if (assetExtended) {
                imguiTaskCollector.assetInspector(*assetExtended);
                imguiTaskCollector.materialEditor(*assetExtended);
                if (!assetExtended->asset.materialVariants.empty()) {
                    imguiTaskCollector.materialVariants(*assetExtended);
                }
                imguiTaskCollector.sceneHierarchy(*assetExtended);
                imguiTaskCollector.nodeInspector(*assetExtended);

                if (!assetExtended->asset.animations.empty()) {
                    imguiTaskCollector.animations(*assetExtended);
                }
            }
            if (const auto &iblInfo = appState.imageBasedLightingProperties) {
                imguiTaskCollector.imageBasedLighting(*iblInfo, vku::toUint64(skyboxResources->imGuiEqmapTextureDescriptorSet));
            }
            imguiTaskCollector.rendererSetting(*renderer);
            if (assetExtended) {
                imguiTaskCollector.imguizmo(*renderer, lastMouseEnteredViewIndex, *assetExtended);
            }
            else {
                imguiTaskCollector.imguizmo(*renderer, lastMouseEnteredViewIndex);
            }

            if (drawSelectionRectangle) {
                const ImVec2 startPos = toImVec2(*lastMouseDownPosition);

                ImRect region { startPos, toImVec2(window.getCursorPos()) };
                if (region.Min.x > region.Max.x) {
                    std::swap(region.Min.x, region.Max.x);
                }
                if (region.Min.y > region.Max.y) {
                    std::swap(region.Min.y, region.Max.y);
                }

                for (const ImRect &clipRect : renderer->getViewportRects(passthruRect)) {
                    if (clipRect.Contains(startPos)) {
                        region.ClipWith(clipRect);
                        break;
                    }
                }

                ImGui::GetBackgroundDrawList()->AddRectFilled(region.Min, region.Max, ImGui::GetColorU32({ 1.f, 1.f, 1.f, 0.2f }));
                ImGui::GetBackgroundDrawList()->AddRect(region.Min, region.Max, ImGui::GetColorU32({ 1.f, 1.f, 1.f, 1.f }));
            }
        }

        vulkan::Frame &frame = frames[frameIndex % FRAMES_IN_FLIGHT];
        if (frameIndex >= FRAMES_IN_FLIGHT) {
            const vulkan::Frame::ExecutionResult result = frame.getExecutionResult();
            if (auto *indices = get_if<std::vector<std::size_t>>(&result.mousePickingResult)) {
                if (ImGui::GetIO().KeyCtrl) {
                    assetExtended->selectedNodes.insert_range(*indices);
                }
                else {
                    assetExtended->selectedNodes = { std::from_range, *indices };
                }
            }
            else if (auto *index = get_if<std::size_t>(&result.mousePickingResult)) {
                assetExtended->hoveringNode = *index;
            }
            else if (assetExtended) {
                assetExtended->hoveringNode.reset();
            }
        }

        if (auto &retained = retainedAssetExtended[frameIndex % FRAMES_IN_FLIGHT]) {
            // The previous execution of the frame requested destroying the asset, and now the request can be done
            // (as ImGui will not refer its texture).
            retained.reset();
        }

        graphicsCommandPool.reset();
        sharedDataUpdateCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        const glm::vec2 framebufferScale = window.getFramebufferSize() / window.getSize();

        // As we're going to update the frame resource from here, frameDeferredTask has to be reset. It can be done by
        // calling FrameDeferredTask::executeAndReset() in here, but there's a problem: what we updated in the previous
        // frame may useless, by either the current frame need to cancel it (e.g. changing asset) or current frame also
        // need to update the same thing (e.g. playing animation that changes the node world transforms).
        //
        // There's good solution for this: we're going to process the collected task, and will cancel the deferred task
        // if necessary. frameDeferredTask will be replaced with the newly created one, and the existing one will be
        // retained until the collected tasks are processed. After the processing finished, it will be executed.
        vulkan::FrameDeferredTask currentFrameTask = std::exchange(frameDeferredTask, vulkan::FrameDeferredTask{});

        // Process the collected tasks.
        for (; !tasks.empty(); tasks.pop()) {
            visit(multilambda {
                [this](const control::task::WindowKey &task) {
                    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureKeyboard) return;

                    if (task.action == GLFW_PRESS && assetExtended && !assetExtended->selectedNodes.empty()) {
                        switch (task.key) {
                            case GLFW_KEY_T:
                                renderer->imGuizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
                                break;
                            case GLFW_KEY_R:
                                renderer->imGuizmoOperation = ImGuizmo::OPERATION::ROTATE;
                                break;
                            case GLFW_KEY_S:
                                renderer->imGuizmoOperation = ImGuizmo::OPERATION::SCALE;
                                break;
                        }
                    }
                },
                [this](const control::task::WindowCursorPos &task) {
                    if (lastMouseDownPosition && distance2(*lastMouseDownPosition, task.position) >= 4.0) {
                        drawSelectionRectangle = true;
                    }

                    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) {
                        for (const auto &[viewIndex, rect] : renderer->getViewportRects(passthruRect) | ranges::views::enumerate) {
                            if (rect.Contains(toImVec2(task.position))) {
                                lastMouseEnteredViewIndex = viewIndex;
                                break;
                            }
                        }
                    }
                },
                [&](const control::task::WindowMouseButton &task) {
                    const bool leftMouseButtonPressed = task.button == GLFW_MOUSE_BUTTON_LEFT && task.action == GLFW_RELEASE && lastMouseDownPosition;
                    bool selectionRectanglePopped = false;
                    if (leftMouseButtonPressed) {
                        lastMouseDownPosition = std::nullopt;

                        if (drawSelectionRectangle) {
                            drawSelectionRectangle = false;
                            selectionRectanglePopped = true;
                        }
                    }

                    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

                    if (task.button == GLFW_MOUSE_BUTTON_LEFT && task.action == GLFW_PRESS) {
                        lastMouseDownPosition = window.getCursorPos();
                    }
                    else if (leftMouseButtonPressed && assetExtended && !selectionRectanglePopped) {
                        if (assetExtended->hoveringNode) {
                            if (ImGui::GetIO().KeyCtrl) {
                                // Toggle the hovering node's selection.
                                if (auto it = assetExtended->selectedNodes.find(*assetExtended->hoveringNode); it != assetExtended->selectedNodes.end()) {
                                    assetExtended->selectedNodes.erase(it);
                                }
                                else {
                                    assetExtended->selectedNodes.emplace_hint(it, *assetExtended->hoveringNode);
                                }
                                tasks.emplace(std::in_place_type<control::task::NodeSelectionChanged>);
                            }
                            else if (assetExtended->selectedNodes.size() != 1 || (*assetExtended->hoveringNode != *assetExtended->selectedNodes.begin())) {
                                // Unless there's only 1 selected node and is the same as the hovering node, change selection
                                // to the hovering node.
                                assetExtended->selectedNodes = { *assetExtended->hoveringNode };
                                tasks.emplace(std::in_place_type<control::task::NodeSelectionChanged>);
                            }
                            global::shouldNodeInSceneHierarchyScrolledToBeVisible = true;
                        }
                        else {
                            assetExtended->selectedNodes.clear();
                            tasks.emplace(std::in_place_type<control::task::NodeSelectionChanged>);
                        }
                    }
                },
                [this](concepts::one_of<control::task::WindowScroll, control::task::WindowTrackpadZoom> auto const &task) {
                    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

                    const double scale = multilambda {
                        [](const control::task::WindowScroll &scroll) { return scroll.offset.y; },
                        [](const control::task::WindowTrackpadZoom &zoom) { return zoom.scale; }
                    }(task);

                    const auto scaleCamera = [&](control::Camera &camera) {
                        const float factor = std::powf(1.01f, -scale);
                        if (auto *orthographic = get_if<control::Camera::Orthographic>(&camera.projection)) {
                            orthographic->ymag *= factor;
                        }
                        else {
                            const glm::vec3 displacementToTarget = camera.direction * camera.targetDistance;
                            camera.targetDistance *= factor;
                            camera.position += (1.f - factor) * displacementToTarget;
                        }
                    };

                    if (ImGui::GetIO().KeyCtrl) {
                        std::ranges::for_each(renderer->cameras, scaleCamera);
                    }
                    else {
                        for (auto &&[camera, rect] : std::views::zip(renderer->cameras, renderer->getViewportRects(passthruRect))) {
                            if (rect.Contains(toImVec2(window.getCursorPos()))) {
                                scaleCamera(camera);
                                break; // Only one camera will be adjusted.
                            }
                        }
                    }
                },
                [this](const control::task::WindowTrackpadRotate &task) {
                    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

                    const auto rotateCamera = [&](control::Camera &camera) {
                        // Rotate the camera around the Y-axis lied on the target point.
                        const glm::vec3 target = camera.position + camera.direction * camera.targetDistance;
                        const glm::mat4 rotation = rotate(-glm::radians<float>(task.angle), glm::vec3 { 0.f, 1.f, 0.f });
                        camera.direction = glm::mat3 { rotation } * camera.direction;
                        camera.position = target - camera.direction * camera.targetDistance;
                    };

                    if (ImGui::GetIO().KeyCtrl) {
                        std::ranges::for_each(renderer->cameras, rotateCamera);
                    }
                    else {
                        for (auto &&[camera, rect] : std::views::zip(renderer->cameras, renderer->getViewportRects(passthruRect))) {
                            if (rect.Contains(toImVec2(window.getCursorPos()))) {
                                rotateCamera(camera);
                                break; // Only one camera will be adjusted.
                            }
                        }
                    }
                },
                [&](const control::task::WindowDrop &task) {
                    // Prevent drag-and-drop when any dialog is opened.
                    if (gui::popup::isModalPopupOpened()) return;

                    if (task.paths.empty()) return;

                    static constexpr auto supportedSkyboxExtensions = {
                        ".jpg",
                        ".jpeg",
                        ".png",
                        ".hdr",
                        #ifdef SUPPORT_EXR_SKYBOX
                        ".exr",
                        #endif
                    };

                    const std::filesystem::path &path = task.paths[0];
                    if (std::filesystem::is_directory(path)) {
                        // If directory contains glTF file, load it.
                        for (const std::filesystem::path &childPath : std::filesystem::directory_iterator { path }) {
                            if (ranges::one_of(childPath.extension(), { ".gltf", ".glb" })) {
                                tasks.emplace(std::in_place_type<control::task::LoadGltf>, childPath);
                                return;
                            }
                        }
                    }
                    else if (const std::filesystem::path extension = path.extension(); ranges::one_of(extension, { ".gltf", ".glb" })) {
                        tasks.emplace(std::in_place_type<control::task::LoadGltf>, path);
                    }
                    else if (std::ranges::contains(supportedSkyboxExtensions, extension)) {
                        tasks.emplace(std::in_place_type<control::task::LoadEqmap>, path);
                    }
                },
                [this](concepts::one_of<control::task::WindowSize, control::task::WindowContentScale> auto const &task) {
                    handleSwapchainResize();
                },
                [&](const control::task::ChangePassthruRect &task) {
                    passthruRect = task.newRect;

                    vk::Extent2D extent {
                        static_cast<std::uint32_t>(std::ceil(framebufferScale.x * task.newRect.GetWidth())),
                        static_cast<std::uint32_t>(std::ceil(framebufferScale.y * task.newRect.GetHeight())),
                    };
                    switch (renderer->cameras.size()) {
                        case 2:
                            extent.width = math::divCeil(extent.width, 2U);
                            break;
                        case 4:
                            extent.width = math::divCeil(extent.width, 2U);
                            extent.height = math::divCeil(extent.height, 2U);
                            break;
                    }

                    currentFrameTask.setViewportExtent(extent);
                    frameDeferredTask.setViewportExtent(extent);
                },
                [&](const control::task::ChangeViewCount &task) {
                    gpu.device.waitIdle();

                    if (renderer->cameras.size() < task.viewCount) {
                        // Extend vector with last element.
                        std::fill_n(std::back_inserter(renderer->cameras), task.viewCount - renderer->cameras.size(), renderer->cameras.back());
                    }
                    else {
                        renderer->cameras.resize(task.viewCount);
                        lastMouseEnteredViewIndex = task.viewCount - 1;
                    }

                    if (task.viewCount > 1) {
                        // TODO: support multiview frustum culling
                        renderer->frustumCullingMode = Renderer::FrustumCullingMode::Off;
                    }

                    sharedData.setViewCount(renderer->cameras.size());
                    regenerateDrawCommands.fill(true);

                    vk::Extent2D extent {
                        static_cast<std::uint32_t>(std::ceil(framebufferScale.x * passthruRect.GetWidth())),
                        static_cast<std::uint32_t>(std::ceil(framebufferScale.y * passthruRect.GetHeight())),
                    };
                    switch (renderer->cameras.size()) {
                        case 2:
                            extent.width = math::divCeil(extent.width, 2U);
                            break;
                        case 4:
                            extent.width = math::divCeil(extent.width, 2U);
                            extent.height = math::divCeil(extent.height, 2U);
                            break;
                    }

                    currentFrameTask.setViewportExtent(extent);
                    frameDeferredTask.setViewportExtent(extent);
                },
                [&](const control::task::LoadGltf &task) {
                    for (auto name : control::ImGuiTaskCollector::assetPopupNames) {
                        gui::popup::close(name);
                    }
                    retainedAssetExtended[frameIndex % FRAMES_IN_FLIGHT] = assetExtended;

                    loadGltf(task.path);

                    // Adjust the camera based on the scene enclosing sphere.
                    const auto &[center, radius] = assetExtended->sceneMiniball.get();
                    const float aspectRatio = [&] {
                        vk::Extent2D extent {
                            static_cast<std::uint32_t>(std::ceil(framebufferScale.x * passthruRect.GetWidth())),
                            static_cast<std::uint32_t>(std::ceil(framebufferScale.y * passthruRect.GetHeight())),
                        };
                        switch (renderer->cameras.size()) {
                            case 2:
                                extent.width = math::divCeil(extent.width, 2U);
                                break;
                            case 4:
                                extent.width = math::divCeil(extent.width, 2U);
                                extent.height = math::divCeil(extent.height, 2U);
                                break;
                        }
                        return vku::aspect(extent);
                    }();
                    for (control::Camera &camera : renderer->cameras) {
                        camera.adjustMiniball(glm::gtc::make_vec3(center.data()), radius, aspectRatio);
                    }

                    // All planned updates related to the previous glTF asset have to be canceled.
                    currentFrameTask.resetAssetRelated();
                    frameDeferredTask.resetAssetRelated();
                    transformedNodes.clear();

                    regenerateDrawCommands.fill(true);
                },
                [&](control::task::CloseGltf) {
                    for (auto name : control::ImGuiTaskCollector::assetPopupNames) {
                        gui::popup::close(name);
                    }
                    retainedAssetExtended[frameIndex % FRAMES_IN_FLIGHT] = assetExtended;

                    closeGltf();

                    // All planned updates related to the previous glTF asset have to be canceled.
                    currentFrameTask.resetAssetRelated();
                    frameDeferredTask.resetAssetRelated();
                    transformedNodes.clear();
                },
                [this](const control::task::LoadEqmap &task) {
                    loadEqmap(task.path);
                },
                [&](control::task::ChangeScene task) {
                    assetExtended->setScene(task.newSceneIndex);

                    frame.gltfAsset->updateNodeWorldTransformScene(task.newSceneIndex);
                    frameDeferredTask.updateNodeWorldTransformScene(task.newSceneIndex);

                    // Adjust the camera based on the scene enclosing sphere.
                    const auto &[center, radius] = assetExtended->sceneMiniball.get();
                    const float aspectRatio = [&] {
                        vk::Extent2D extent {
                            static_cast<std::uint32_t>(std::ceil(framebufferScale.x * passthruRect.GetWidth())),
                            static_cast<std::uint32_t>(std::ceil(framebufferScale.y * passthruRect.GetHeight())),
                        };
                        switch (renderer->cameras.size()) {
                            case 2:
                                extent.width = math::divCeil(extent.width, 2U);
                                break;
                            case 4:
                                extent.width = math::divCeil(extent.width, 2U);
                                extent.height = math::divCeil(extent.height, 2U);
                                break;
                        }
                        return vku::aspect(extent);
                    }();
                    for (control::Camera &camera : renderer->cameras) {
                        camera.adjustMiniball(glm::gtc::make_vec3(center.data()), radius, aspectRatio);
                    }

                    transformedNodes.clear(); // They are all related to the previous glTF asset.
                    regenerateDrawCommands.fill(true);
                },
                [&](control::task::NodeVisibilityChanged task) {
                    // TODO: instead of calculate all draw commands, update only changed stuffs based on task.nodeIndex.
                    regenerateDrawCommands.fill(true);
                },
                [this](control::task::NodeSelectionChanged) {
                    // If selected nodes have a single material, show it in the Material Editor window.
                    std::optional<std::size_t> uniqueMaterialIndex = std::nullopt;
                    for (std::size_t nodeIndex : assetExtended->selectedNodes) {
                        const auto &meshIndex = assetExtended->asset.nodes[nodeIndex].meshIndex;
                        if (!meshIndex) continue;

                        for (const fastgltf::Primitive &primitive : assetExtended->asset.meshes[*meshIndex].primitives) {
                            if (primitive.materialIndex) {
                                if (!uniqueMaterialIndex) {
                                    uniqueMaterialIndex.emplace(*primitive.materialIndex);
                                }
                                else if (*uniqueMaterialIndex != *primitive.materialIndex) {
                                    // The input nodes contain at least 2 materials.
                                    return;
                                }
                            }
                        }
                    }

                    if (uniqueMaterialIndex) {
                        assetExtended->imGuiSelectedMaterialIndex.emplace(*uniqueMaterialIndex);
                    }
                },
                [this](const control::task::HoverNodeFromGui &task) {
                    assetExtended->hoveringNode.emplace(task.nodeIndex);
                },
                [&](const control::task::NodeLocalTransformChanged &task) {
                    transformedNodes.push_back(task.nodeIndex);
                },
                [&](const control::task::NodeWorldTransformChanged &task) {
                    // It merges the current node world transform update request with the previous requests.
                    currentFrameTask.updateNodeWorldTransform(task.nodeIndex);
                    frameDeferredTask.updateNodeWorldTransform(task.nodeIndex);

                    assetExtended->sceneMiniball.invalidate();
                },
                [&](control::task::MaterialAdded) {
                    vulkan::gltf::AssetExtended &vkAsset = *dynamic_cast<vulkan::gltf::AssetExtended*>(assetExtended.get());
                    if (!vkAsset.materialBuffer.canAddMaterial()) {
                        // Enlarge the material buffer.
                        gpu.device.waitIdle();
                        if (auto oldMaterialBuffer = vkAsset.materialBuffer.enlarge(sharedDataUpdateCommandBuffer)) {
                            sharedDataStagingBuffers.push_back(std::move(*oldMaterialBuffer));
                            hasUpdateData = true;
                        }

                        // Update asset descriptor set by each frame to make them point to the new material buffer.
                        INDEX_SEQ(Is, FRAMES_IN_FLIGHT, {
                            sharedData.gpu.device.updateDescriptorSets(
                                std::array { (get<Is>(frames).assetDescriptorSet.template getWrite<2>(vkAsset.materialBuffer.descriptorInfo))... },
                                {});
                        });
                    }

                    // Add the new material to the material buffer.
                    hasUpdateData |= vkAsset.materialBuffer.add(assetExtended->asset, assetExtended->asset.materials.back(), sharedDataUpdateCommandBuffer);
                },
                [&](const control::task::MaterialPropertyChanged &task) {
                    const fastgltf::Material &changedMaterial = assetExtended->asset.materials[task.materialIndex];
                    auto* const vkAssetExtended = dynamic_cast<vulkan::gltf::AssetExtended*>(assetExtended.get());

                    switch (task.property) {
                        using Property = control::task::MaterialPropertyChanged::Property;
                        case Property::AlphaMode:
                        case Property::Unlit:
                        case Property::DoubleSided:
                            regenerateDrawCommands.fill(true);
                            break;
                        case Property::AlphaCutoff:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::alphaCutOff>(
                                task.materialIndex,
                                changedMaterial.alphaCutoff,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::BaseColorFactor:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::baseColorFactor>(
                                task.materialIndex,
                                glm::make_vec4(changedMaterial.pbrData.baseColorFactor.data()),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::BaseColorTextureTransform:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::baseColorTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(changedMaterial.pbrData.baseColorTexture->transform.get()),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::EmissiveStrength: {
                            const auto it = assetExtended->bloomMaterials.find(task.materialIndex);
                            const bool useBloom = assetExtended->asset.materials[task.materialIndex].emissiveStrength > 1.f;

                            // Material emissive strength is changed to 1.
                            if (it != assetExtended->bloomMaterials.end() && !useBloom) {
                                assetExtended->bloomMaterials.erase(it);
                                regenerateDrawCommands.fill(true);
                            }
                            // Material emissive strength is changed from 1.
                            else if (it == assetExtended->bloomMaterials.end() && useBloom) {
                                assetExtended->bloomMaterials.emplace_hint(it, task.materialIndex);
                                regenerateDrawCommands.fill(true);
                            }
                            [[fallthrough]]; // materialBuffer also needs to be updated.
                        }
                        case Property::Emissive:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::emissive>(
                                task.materialIndex,
                                changedMaterial.emissiveStrength * glm::make_vec3(changedMaterial.emissiveFactor.data()),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::EmissiveTextureTransform:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::emissiveTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(changedMaterial.emissiveTexture->transform.get()),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::Ior:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::ior>(
                                task.materialIndex,
                                changedMaterial.ior,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::MetallicFactor:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::metallicFactor>(
                                task.materialIndex,
                                changedMaterial.pbrData.metallicFactor,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::MetallicRoughnessTextureTransform:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::metallicRoughnessTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(changedMaterial.pbrData.metallicRoughnessTexture->transform.get()),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::NormalScale:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::normalScale>(
                                task.materialIndex,
                                changedMaterial.normalTexture->scale,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::NormalTextureTransform:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::normalTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(changedMaterial.normalTexture->transform.get()),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::OcclusionStrength:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::occlusionStrength>(
                                task.materialIndex,
                                changedMaterial.occlusionTexture->strength,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::OcclusionTextureTransform:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::occlusionTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(changedMaterial.occlusionTexture->transform.get()),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::RoughnessFactor:
                            hasUpdateData |= vkAssetExtended->materialBuffer.update<&vulkan::shader_type::Material::roughnessFactor>(
                                task.materialIndex,
                                changedMaterial.pbrData.roughnessFactor,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::TextureTransformEnabled:
                            if (!assetExtended->isTextureTransformUsed) {
                                assetExtended->asset.extensionsUsed.push_back("KHR_texture_transform");
                                assetExtended->isTextureTransformUsed = true;

                                // Asset is loaded without KHR_texture_transform extension, and all pipelines were created
                                // with texture transform disabled. Pipelines need to be recreated.
                                regenerateDrawCommands.fill(true);
                            }
                            break;
                    }
                },
                [&](const control::task::PrimitiveMaterialChanged &task) {
                    vkgltf::PrimitiveBuffer &primitiveBuffer = dynamic_cast<vulkan::gltf::AssetExtended*>(assetExtended.get())->primitiveBuffer;
                    const std::size_t primitiveIndex = primitiveBuffer.getPrimitiveIndex(*task.primitive);
                    std::int32_t &dstData = primitiveBuffer.mappedData[primitiveIndex].materialIndex;

                    if (vku::contains(gpu.allocator.getAllocationMemoryProperties(primitiveBuffer.allocation), vk::MemoryPropertyFlagBits::eHostVisible)) {
                        gpu.device.waitIdle();

                        if (task.primitive->materialIndex) {
                            dstData = *task.primitive->materialIndex + 1;
                        }
                        else {
                            dstData = 0;
                        }
                    }
                    else {
                        const vk::DeviceSize dstOffset
                            = reinterpret_cast<const std::byte*>(&dstData)
                            - reinterpret_cast<const std::byte*>(primitiveBuffer.mappedData.data());

                        std::uint32_t data = 0;
                        if (task.primitive->materialIndex) {
                            data = *task.primitive->materialIndex + 1;
                        }

                        sharedDataUpdateCommandBuffer.updateBuffer<std::remove_cvref_t<decltype(dstData)>>(
                            primitiveBuffer, dstOffset, data);
                        hasUpdateData = true;
                    }

                    // Draw commands need to be regenerated if changed material has different alpha mode/unlit/double-sided.
                    regenerateDrawCommands.fill(true);
                },
                [&](const control::task::MorphTargetWeightChanged &task) {
                    // It merges the current node target weight update request with the previous requests.
                    currentFrameTask.updateNodeTargetWeights(task.nodeIndex, task.targetWeightStartIndex, task.targetWeightCount);
                    frameDeferredTask.updateNodeTargetWeights(task.nodeIndex, task.targetWeightStartIndex, task.targetWeightCount);

                    assetExtended->sceneMiniball.invalidate();
                },
                [&](control::task::BloomModeChanged) {
                    // Primitive rendering pipelines have to be recreated to use shader stencil export or not.
                    regenerateDrawCommands.fill(true);
                },
            }, tasks.front());
        }

        if (!transformedNodes.empty()) {
            // Remove duplicates in transformedNodes.
            std::ranges::sort(transformedNodes);
            const auto [begin, end] = std::ranges::unique(transformedNodes);
            transformedNodes.erase(begin, end);

            // Sort transformedNodes by their node level in the scene.
            std::ranges::sort(transformedNodes, {}, LIFT(assetExtended->sceneNodeLevels.operator[]));

            std::vector visited(assetExtended->asset.nodes.size(), false);
            for (std::size_t nodeIndex : transformedNodes) {
                // If node is marked as visited, its world transform is already updated by its ancestor node. Skipping it.
                if (visited[nodeIndex]) continue;

                // TODO.CXX26: std::optional<const fastgltf::math::fmat4x4&> can ditch the unnecessary copying.
                fastgltf::math::fmat4x4 baseMatrix { 1.f };
                if (const auto &parentNodeIndex = assetExtended->sceneInverseHierarchy.parentNodeIndices[nodeIndex]) {
                    baseMatrix = assetExtended->nodeWorldTransforms[*parentNodeIndex];
                }
                const fastgltf::math::fmat4x4 nodeWorldTransform = fastgltf::getTransformMatrix(assetExtended->asset.nodes[nodeIndex], baseMatrix);

                // Update current and descendants world transforms and mark them as visited.
                traverseNode(assetExtended->asset, nodeIndex, [&](std::size_t nodeIndex, const fastgltf::math::fmat4x4 &worldTransform) noexcept {
                    assetExtended->nodeWorldTransforms[nodeIndex] = worldTransform;

                    assert(!visited[nodeIndex] && "This must be visited");
                    visited[nodeIndex] = true;
                }, nodeWorldTransform);

                // Update GPU side world transform data.
                // It merges the current node world transform update request with the previous requests.
                currentFrameTask.updateNodeWorldTransformHierarchical(nodeIndex);
                frameDeferredTask.updateNodeWorldTransformHierarchical(nodeIndex);
            }

            assetExtended->sceneMiniball.invalidate();
        }

        // Tighten camera's near/far plane based on the updated scene bounds.
        if (assetExtended) {
            const auto &[center, radius] = assetExtended->sceneMiniball.get();
            for (control::Camera &camera : renderer->cameras) {
                if (camera.automaticNearFarPlaneAdjustment) {
                    camera.tightenNearFar(glm::make_vec3(center.data()), radius);
                }
            }
        }

        if (hasUpdateData) {
            sharedDataUpdateCommandBuffer.end();

            vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
            gpu.queues.graphicsPresent.submit(vk::SubmitInfo { {}, {}, sharedDataUpdateCommandBuffer }, *fence);
            std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling
            sharedDataStagingBuffers.clear();
        }

        // Update frame resources.
        currentFrameTask.executeAndReset(frame);

        const vk::Offset2D passthruOffset = {
            static_cast<std::int32_t>(framebufferScale.x * passthruRect.Min.x),
            static_cast<std::int32_t>(framebufferScale.y * passthruRect.Min.y),
        };

        vk::Extent2D viewportExtent {
            static_cast<std::uint32_t>(std::ceil(framebufferScale.x * passthruRect.GetWidth())),
            static_cast<std::uint32_t>(std::ceil(framebufferScale.y * passthruRect.GetHeight())),
        };
        switch (renderer->cameras.size()) {
            case 2:
                viewportExtent.width = math::divCeil(viewportExtent.width, 2U);
                break;
            case 4:
                viewportExtent.width = math::divCeil(viewportExtent.width, 2U);
                viewportExtent.height = math::divCeil(viewportExtent.height, 2U);
                break;
        }

        frame.update({
            .passthruOffset = passthruOffset,
            .gltf = value_if(static_cast<bool>(assetExtended), [&] {
                return vulkan::Frame::ExecutionTask::Gltf {
                    .regenerateDrawCommands = std::exchange(regenerateDrawCommands[frameIndex % FRAMES_IN_FLIGHT], false),
                    .mousePickingInput = [&] -> std::optional<std::pair<std::uint32_t, vk::Rect2D>> {
                        const ImVec2 cursorPos = toImVec2(window.getCursorPos());
                        if (drawSelectionRectangle) {
                            const ImVec2 startPos = toImVec2(*lastMouseDownPosition);

                            ImRect selectionRect { startPos, cursorPos };
                            if (selectionRect.Min.x > selectionRect.Max.x) {
                                std::swap(selectionRect.Min.x, selectionRect.Max.x);
                            }
                            if (selectionRect.Min.y > selectionRect.Max.y) {
                                std::swap(selectionRect.Min.y, selectionRect.Max.y);
                            }

                            for (const auto &[viewIndex, clipRect] : renderer->getViewportRects(passthruRect) | ranges::views::enumerate) {
                                if (clipRect.Contains(startPos)) {
                                    selectionRect.ClipWith(clipRect);

                                    vk::Extent2D extent {
                                        static_cast<std::uint32_t>(framebufferScale.x * selectionRect.GetWidth()),
                                        static_cast<std::uint32_t>(framebufferScale.y * selectionRect.GetHeight()),
                                    };

                                    // If its size is zero, mouse picking should not be performed.
                                    if (extent.width == 0 || extent.height == 0) {
                                        return std::nullopt;
                                    }

                                    return std::pair<std::uint32_t, vk::Rect2D> {
                                        viewIndex,
                                        vk::Rect2D {
                                            vk::Offset2D {
                                                static_cast<std::int32_t>(framebufferScale.x * (selectionRect.Min.x - clipRect.Min.x)),
                                                static_cast<std::int32_t>(framebufferScale.y * (selectionRect.Min.y - clipRect.Min.y)),
                                            },
                                            extent,
                                        },
                                    };
                                }
                            }
                            std::unreachable(); // drawSelectionRectangle == true but no selection rectangle.
                        }

                        if (ImGui::GetIO().WantCaptureMouse) {
                            return std::nullopt;
                        }

                        for (const auto &[viewIndex, clipRect] : renderer->getViewportRects(passthruRect) | ranges::views::enumerate) {
                            if (clipRect.Contains(cursorPos)) {
                                return std::pair<std::uint32_t, vk::Rect2D> {
                                    viewIndex,
                                    vk::Rect2D {
                                        vk::Offset2D {
                                            static_cast<std::int32_t>(framebufferScale.x * (cursorPos.x - clipRect.Min.x)),
                                            static_cast<std::int32_t>(framebufferScale.y * (cursorPos.y - clipRect.Min.y)),
                                        },
                                        vk::Extent2D { 1, 1 },
                                    },
                                };
                            }
                        }

                        return std::nullopt;
                    }(),
                };
            }),
        });

        frame.recordCommandsAndSubmit(swapchain);
    }
    gpu.device.waitIdle();
}

vk_gltf_viewer::MainApp::ImGuiContext::ImGuiContext(const control::AppWindow &window, vk::Instance instance, const vulkan::Gpu &gpu) {
    ImGui::CheckVersion();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();    
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImFontConfig fontConfig;
    if (std::filesystem::exists(DEFAULT_FONT_PATH)) {
        io.Fonts->AddFontFromFileTTF(DEFAULT_FONT_PATH, 0.f, &fontConfig);
    }
    else {
        std::cerr << "Your system doesn't have expected system font at " DEFAULT_FONT_PATH ". Low-resolution font will be used instead.";
        io.Fonts->AddFontDefault(&fontConfig);
    }

    fontConfig.MergeMode = true;
    constexpr ImWchar fontAwesomeIconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromMemoryCompressedBase85TTF(asset::font::fontawesome_webfont_ttf_compressed_data_base85, 0.f, &fontConfig, fontAwesomeIconRanges);

    ImGui_ImplGlfw_InitForVulkan(window, true);
    const vk::Format colorAttachmentFormat = gpu.supportSwapchainMutableFormat ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb;
    ImGui_ImplVulkan_InitInfo initInfo {
        .ApiVersion = vk::makeApiVersion(0, 1, 2, 0),
        .Instance = instance,
        .PhysicalDevice = *gpu.physicalDevice,
        .Device = *gpu.device,
        .QueueFamily = gpu.queueFamilies.graphicsPresent,
        .Queue = gpu.queues.graphicsPresent,
        .DescriptorPoolSize = 512,
        // ImGui requires ImGui_ImplVulkan_InitInfo::{MinImageCount,ImageCount} ≥ 2 (I don't know why...).
        .MinImageCount = std::max(FRAMES_IN_FLIGHT, 2U),
        .ImageCount = std::max(FRAMES_IN_FLIGHT, 2U),
        .UseDynamicRendering = true,
        .PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfo {
            {},
            colorAttachmentFormat,
        },
    };
    ImGui_ImplVulkan_Init(&initInfo);

    userData.platformResource = std::make_unique<vulkan::imgui::PlatformResource>(gpu);
    io.UserData = &userData;
    userData.registerSettingHandler();
}

vk_gltf_viewer::MainApp::ImGuiContext::~ImGuiContext() {
    // Since userData.platformResource is instantiated under ImGui_ImplVulkan context, it must be destroyed before shutdown ImGui_ImplVulkan.
    userData.platformResource.reset();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

vk_gltf_viewer::MainApp::SkyboxResources::~SkyboxResources() {
    ImGui_ImplVulkan_RemoveTexture(imGuiEqmapTextureDescriptorSet);
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

    vk::raii::Instance instance { context, vk::StructureChain {
        vk::InstanceCreateInfo {
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
        },
    #if __APPLE__
        vk::ExportMetalObjectCreateInfoEXT { vk::ExportMetalObjectTypeFlagBitsEXT::eMetalCommandQueue },
    #endif
    }.get() };
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
    return instance;
}

vk_gltf_viewer::MainApp::ImageBasedLightingResources vk_gltf_viewer::MainApp::createDefaultImageBasedLightingResources() const {
    vku::AllocatedBuffer sphericalHarmonicsBuffer {
        gpu.allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(glm::vec3) * 9,
            vk::BufferUsageFlagBits::eUniformBuffer,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
            vma::MemoryUsage::eAutoPreferDevice,
        },
    };
    constexpr glm::vec3 data[9] = { glm::vec3 { 1.f } };
    gpu.allocator.copyMemoryToAllocation(data, sphericalHarmonicsBuffer.allocation, 0, sizeof(data));

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
        std::move(sphericalHarmonicsBuffer),
        std::move(prefilteredmapImage),
        std::move(prefilteredmapImageView),
    };
}

vk::raii::Sampler vk_gltf_viewer::MainApp::createEqmapSampler() const {
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

vku::AllocatedImage vk_gltf_viewer::MainApp::createBrdfmapImage() const {
    return { gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eR16G16Unorm,
        vk::Extent3D { 512, 512, 1 },
        1, 1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        ibl::BrdfmapRenderPipeline::requiredResultImageUsageFlags | vk::ImageUsageFlagBits::eSampled,
    } };
}

void vk_gltf_viewer::MainApp::loadGltf(const std::filesystem::path &path) {
    std::shared_ptr<vulkan::gltf::AssetExtended> vkAssetExtended;
    vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
    vkgltf::StagingBufferStorage stagingBufferStorage { gpu.device, transferCommandPool, gpu.queues.transfer };
    try {
        vkAssetExtended = std::make_shared<vulkan::gltf::AssetExtended>(path, gpu, sharedData.fallbackTexture, stagingBufferStorage);
    }
    catch (gltf::AssetProcessError error) {
        std::println(std::cerr, "The glTF file cannot be processed because of an error: {}", to_string(error));
        closeGltf();
        return;
    }
    catch (fastgltf::Error error) {
        // If error is due to missing or unknown required extension, show a message and return.
        if (ranges::one_of(error, { fastgltf::Error::MissingExtensions, fastgltf::Error::UnknownRequiredExtension })) {
            std::println(std::cerr, "The glTF file requires an extension that is not supported by this application.");
            closeGltf();
            return;
        }
        else {
            // Application fault.
            std::rethrow_exception(std::current_exception());
        }
    }

    assetExtended = vkAssetExtended;

    // TODO: I'm aware that there are better solutions compare to the waitIdle, but I don't have much time for it
    //  so I'll just use it for now.
    gpu.device.waitIdle();
    sharedData.assetExtended = std::move(vkAssetExtended);
    for (vulkan::Frame &frame : frames) {
        frame.updateAsset();
    }

    // Change window title.
    window.setTitle(PATH_C_STR(path.filename()));

    // Update AppState.
    appState.pushRecentGltfPath(path);

    renderer->bloom.set_active(!assetExtended->bloomMaterials.empty());
}

void vk_gltf_viewer::MainApp::closeGltf() {
    gpu.device.waitIdle();

    for (vulkan::Frame &frame : frames) {
        frame.gltfAsset.reset();
    }
    sharedData.assetExtended.reset();
    assetExtended.reset();

    window.setTitle("Vulkan glTF Viewer");
}

void vk_gltf_viewer::MainApp::loadEqmap(const std::filesystem::path &eqmapPath) {
    vk::Extent2D eqmapImageExtent;
    vk::Format eqmapImageFormat;
    const vku::AllocatedBuffer eqmapStagingBuffer = [&]() {
        std::unique_ptr<std::byte[]> data; // It should be freed after copying to the staging buffer, therefore declared as unique_ptr.
        const std::filesystem::path extension = eqmapPath.extension();
#ifdef SUPPORT_EXR_SKYBOX
        if (extension == ".exr") {
            Imf::InputFile file{ PATH_C_STR(eqmapPath), static_cast<int>(std::thread::hardware_concurrency()) };

            const Imath::Box2i dw = file.header().dataWindow();
            eqmapImageExtent.width = static_cast<std::uint32_t>(dw.max.x - dw.min.x + 1);
            eqmapImageExtent.height = static_cast<std::uint32_t>(dw.max.y - dw.min.y + 1);
            eqmapImageFormat = vk::Format::eR32G32B32A32Sfloat;

            data = std::make_unique_for_overwrite<std::byte[]>(4 * eqmapImageExtent.width * eqmapImageExtent.height * sizeof(float));

            // Create frame buffers for each channel.
            // Note: Alpha channel will be ignored.
            Imf::FrameBuffer frameBuffer;
            constexpr std::size_t xStride = sizeof(float[4]);
            const std::size_t yStride = eqmapImageExtent.width * xStride;
            frameBuffer.insert("R", Imf::Slice{ Imf::FLOAT, reinterpret_cast<char*>(data.get()), xStride, yStride });
            frameBuffer.insert("G", Imf::Slice{ Imf::FLOAT, reinterpret_cast<char*>(data.get() + sizeof(float)), xStride, yStride });
            frameBuffer.insert("B", Imf::Slice{ Imf::FLOAT, reinterpret_cast<char*>(data.get() + 2 * sizeof(float)), xStride, yStride });

            file.readPixels(frameBuffer, dw.min.y, dw.max.y);
        }
        else
#endif
        {
            int width, height;
            if (extension == ".hdr") {
                data.reset(reinterpret_cast<std::byte*>(stbi_loadf(PATH_C_STR(eqmapPath), &width, &height, nullptr, 4)));
                eqmapImageFormat = vk::Format::eR32G32B32A32Sfloat;
            }
            else {
                data.reset(reinterpret_cast<std::byte*>(stbi_load(PATH_C_STR(eqmapPath), &width, &height, nullptr, 4)));
                eqmapImageFormat = vk::Format::eR8G8B8A8Srgb;
            }
            if (!data) {
                throw std::runtime_error { std::format("Failed to load image: {}", stbi_failure_reason()) };
            }

            eqmapImageExtent.width = static_cast<std::uint32_t>(width);
            eqmapImageExtent.height = static_cast<std::uint32_t>(height);
        }

        vku::AllocatedBuffer result {
            gpu.allocator,
            vk::BufferCreateInfo {
                {},
                blockSize(eqmapImageFormat) * eqmapImageExtent.width * eqmapImageExtent.height,
                vk::BufferUsageFlagBits::eTransferSrc,
            },
            vma::AllocationCreateInfo {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                vma::MemoryUsage::eAutoPreferHost,
            },
        };
        gpu.allocator.copyMemoryToAllocation(data.get(), result.allocation, 0, result.size);

        return result;
        // After this scope, data will be automatically freed.
    }();

    std::uint32_t eqmapImageMipLevels = 1;
    for (std::uint32_t mipWidth = eqmapImageExtent.width >> 1; mipWidth > 512; mipWidth >>= 1, ++eqmapImageMipLevels);

    const vku::AllocatedImage eqmapImage { gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        eqmapImageFormat,
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

    const std::uint32_t cubemapSize = std::bit_floor(eqmapImageExtent.height / 2);
    vku::AllocatedImage cubemapImage { gpu.allocator, vk::ImageCreateInfo {
        vk::ImageCreateFlagBits::eCubeCompatible,
        vk::ImageType::e2D,
        // Use non-sRGB format as sRGB format is usually not compatible with storage image.
        eqmapImageFormat == vk::Format::eR8G8B8A8Srgb ? vk::Format::eR8G8B8A8Unorm : eqmapImageFormat,
        vk::Extent3D { cubemapSize, cubemapSize, 1 },
        vku::Image::maxMipLevels(cubemapSize), 6,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        cubemap::CubemapComputePipeline::requiredCubemapImageUsageFlags
            | cubemap::SubgroupMipmapComputePipeline::requiredImageUsageFlags
            | ibl::PrefilteredmapComputePipeline::requiredCubemapImageUsageFlags
            | vk::ImageUsageFlagBits::eSampled,
    } };

    const vk::raii::Sampler eqmapSampler { gpu.device, vk::SamplerCreateInfo { {}, vk::Filter::eLinear, vk::Filter::eLinear }.setMaxLod(vk::LodClampNone) };

    const cubemap::CubemapComputePipeline cubemapComputePipeline { gpu.device, eqmapImage, eqmapSampler, cubemapImage };
    const cubemap::SubgroupMipmapComputePipeline subgroupMipmapComputePipeline { gpu.device, cubemapImage, {
        .subgroupSize = gpu.subgroupSize,
        .useShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod,
    } };

    const std::uint32_t prefilteredmapSize = std::min(cubemapSize, 256U);
    vku::AllocatedImage prefilteredmapImage { gpu.allocator, vk::ImageCreateInfo {
        vk::ImageCreateFlagBits::eCubeCompatible,
        vk::ImageType::e2D,
        cubemapImage.format,
        vk::Extent3D { prefilteredmapSize, prefilteredmapSize, 1 },
        vku::Image::maxMipLevels(prefilteredmapSize), 6,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        ibl::PrefilteredmapComputePipeline::requiredPrefilteredmapImageUsageFlags | vk::ImageUsageFlagBits::eSampled,
    } };
    vku::MappedBuffer sphericalHarmonicsBuffer {
        gpu.allocator,
        vk::BufferCreateInfo {
            {},
            ibl::SphericalHarmonicCoefficientComputePipeline::requiredResultBufferSize,
            ibl::SphericalHarmonicCoefficientComputePipeline::requiredResultBufferUsageFlags | vk::BufferUsageFlagBits::eUniformBuffer,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferDevice,
        },
    };

    const ibl::SphericalHarmonicCoefficientComputePipeline sphericalHarmonicCoefficientComputePipeline { gpu.device, gpu.allocator, cubemapImage, sphericalHarmonicsBuffer, {
        .sampleMipLevel = 0,
        .subgroupSize = gpu.subgroupSize,
    } };
    const ibl::PrefilteredmapComputePipeline prefilteredmapComputePipeline { gpu.device, cubemapImage, prefilteredmapImage, {
        .useShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod,
        .specializationConstants = {
            .samples = 1024,
        },
    } };

    // Generate Tone-mapped cubemap.
    const vulkan::rp::CubemapToneMapping cubemapToneMappingRenderPass { gpu.device };
    const vulkan::CubemapToneMappingRenderPipeline cubemapToneMappingRenderPipeline { gpu, cubemapToneMappingRenderPass };

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
            }, visit(identity<vk::CommandPool>, graphicsCommandPool), gpu.queues.graphicsPresent }),
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
            }, visit(identity<vk::CommandPool>, graphicsCommandPool), gpu.queues.graphicsPresent/*, 4*/ },
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
                cubemapComputePipeline.recordCommands(cb);

                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                    {},
                    // Ensure eqmap to cubemap projection finish before generating mipmaps.
                    vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                    {}, {});

                // Generate cubemapImage mipmaps.
                subgroupMipmapComputePipeline.recordCommands(cb);

                cb.pipelineBarrier2KHR({
                    {}, {}, {},
                    vku::unsafeProxy({
                        // cubemapImage : General -> ShaderReadOnlyOptimal.
                        vk::ImageMemoryBarrier2 {
                            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
                            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderSampledRead,
                            vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                            cubemapImage, vku::fullSubresourceRange(),
                        },
                        // prefilteredmapImage: Undefined -> ShaderReadOnlyOptimal.
                        vk::ImageMemoryBarrier2 {
                            {}, {},
                            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
                            {}, vk::ImageLayout::eGeneral, 
                            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                            prefilteredmapImage, vku::fullSubresourceRange(),
                        },
                    }),
                });

                // Generate prefiltered map.
                prefilteredmapComputePipeline.recordCommands(cb);

                // Reduce spherical harmonic coefficients.
                sphericalHarmonicCoefficientComputePipeline.recordCommands(cb);

                cb.pipelineBarrier2KHR({
                    {}, {},
                    vku::unsafeProxy(vk::BufferMemoryBarrier2 {
                        vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                        vk::PipelineStageFlagBits2::eAllCommands, {},
                        gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
                        sphericalHarmonicsBuffer, 0, vk::WholeSize,
                    }),
                    vku::unsafeProxy({
                        vk::ImageMemoryBarrier2 {
                            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderSampledRead,
                            vk::PipelineStageFlagBits2::eAllCommands, {},
                            {}, {},
                            gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
                            cubemapImage, vku::fullSubresourceRange(),
                        },
                        vk::ImageMemoryBarrier2 {
                            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
                            vk::PipelineStageFlagBits2::eAllCommands, {},
                            vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                            gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
                            prefilteredmapImage, vku::fullSubresourceRange(),
                        },
                    }),
                });
            }, visit(identity<vk::CommandPool>, computeCommandPool), gpu.queues.compute }),
        std::forward_as_tuple(
            // Acquire resources' queue family ownership from compute to graphicsPresent (if necessary), and create tone
            // mapped cubemap image (=toneMappedCubemapImage) from high-precision image (=cubemapImage).
            vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
                if (gpu.queueFamilies.compute != gpu.queueFamilies.graphicsPresent) {
                    cb.pipelineBarrier2KHR({
                        {}, {},
                        vku::unsafeProxy(vk::BufferMemoryBarrier2 {
                            {}, {},
                            vk::PipelineStageFlagBits2::eAllCommands, {},
                            gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
                            sphericalHarmonicsBuffer, 0, vk::WholeSize,
                        }),
                        vku::unsafeProxy({
                            vk::ImageMemoryBarrier2 {
                                {}, {},
                                vk::PipelineStageFlagBits2::eAllCommands, {},
                                {}, {},
                                gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
                                cubemapImage, vku::fullSubresourceRange(),
                            },
                            vk::ImageMemoryBarrier2 {
                                {}, {},
                                vk::PipelineStageFlagBits2::eAllCommands, {},
                                vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                                gpu.queueFamilies.compute, gpu.queueFamilies.graphicsPresent,
                                prefilteredmapImage, vku::fullSubresourceRange(),
                            },
                        }),
                    });
                }

                cb.beginRenderPass({
                    *cubemapToneMappingRenderPass,
                    *cubemapToneMappingFramebuffer,
                    vk::Rect2D { { 0, 0 }, vku::toExtent2D(toneMappedCubemapImage.extent) },
                    vku::unsafeProxy<vk::ClearValue>(vk::ClearColorValue{}),
                }, vk::SubpassContents::eInline);

                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *cubemapToneMappingRenderPipeline.pipeline);
                cb.pushDescriptorSetKHR(
                    vk::PipelineBindPoint::eGraphics,
                    *cubemapToneMappingRenderPipeline.pipelineLayout,
                    0, vulkan::CubemapToneMappingRenderPipeline::DescriptorSetLayout::getWriteOne<0>({ {}, *cubemapImageArrayView, vk::ImageLayout::eShaderReadOnlyOptimal }));
                cb.setViewport(0, vku::unsafeProxy(vku::toViewport(vku::toExtent2D(toneMappedCubemapImage.extent))));
                cb.setScissor(0, vku::unsafeProxy(vk::Rect2D { { 0, 0 }, vku::toExtent2D(toneMappedCubemapImage.extent) }));
                cb.draw(3, 1, 0, 0);

                cb.endRenderPass();
            }, visit(identity<vk::CommandPool>, graphicsCommandPool), gpu.queues.graphicsPresent }));

    std::ignore = gpu.device.waitSemaphores({
        {},
        vku::unsafeProxy(timelineSemaphores | ranges::views::deref | std::ranges::to<std::vector>()),
        finalWaitValues
    }, ~0ULL);

    const std::span<glm::vec3> sphericalHarmonicCoefficients = sphericalHarmonicsBuffer.asRange<glm::vec3>();
    for (float multiplier = 4.f * std::numbers::pi_v<float> / (cubemapSize * cubemapSize * 6.f);
        glm::vec3 &v : sphericalHarmonicCoefficients) {
        v *= multiplier;
    }

    // Update AppState.
    renderer->setSkybox({} /* TODO */);
    appState.imageBasedLightingProperties = AppState::ImageBasedLighting {
        .eqmap = {
            .path = eqmapPath,
            .dimension = { eqmapImage.extent.width, eqmapImage.extent.height },
        },
        .cubemap = {
            .size = cubemapSize,
        },
        .prefilteredmap = {
            .size = prefilteredmapSize,
            .roughnessLevels = prefilteredmapImage.mipLevels,
            .sampleCount = 1024,
        }
    };
    std::ranges::copy(
        sphericalHarmonicCoefficients,
        appState.imageBasedLightingProperties->diffuseIrradiance.sphericalHarmonicCoefficients.begin());

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

    vk::raii::ImageView prefilteredmapImageView { gpu.device, prefilteredmapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
    imageBasedLightingResources = {
        std::move(sphericalHarmonicsBuffer).unmap(),
        std::move(prefilteredmapImage),
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

void vk_gltf_viewer::MainApp::handleSwapchainResize() {
    gpu.device.waitIdle();

    // Make process idle state if window is minimized.
    vk::Extent2D swapchainExtent;
    while (!glfwWindowShouldClose(window) && (swapchainExtent = getSwapchainExtent()) == vk::Extent2D{}) {
        std::this_thread::yield();
    }

    // Update swapchain.
    swapchain.setExtent(swapchainExtent);

    // Change swapchain image layout.
    const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
    const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
        recordSwapchainImageLayoutTransitionCommands(cb);
    }, *fence);
    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

    // Update frame shared data and frames.
    sharedData.handleSwapchainResize(swapchainExtent, swapchain.images);
}

void vk_gltf_viewer::MainApp::recordSwapchainImageLayoutTransitionCommands(vk::CommandBuffer cb) const {
    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, {}, {},
        swapchain.images
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
