module;

#include <cassert>

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

module vk_gltf_viewer;
import :MainApp;

import std;
import asset;
import cubemap;
import ibl;
import imgui.glfw;
import imgui.vulkan;
import :vulkan.Frame;
import :vulkan.pipeline.CubemapToneMappingRenderer;

import vk_gltf_viewer.AppState;
import vk_gltf_viewer.control.AppWindow;
import vk_gltf_viewer.global;
import vk_gltf_viewer.gltf.algorithm.miniball;
import vk_gltf_viewer.gltf.Animation;
import vk_gltf_viewer.gltf.data_structure.SceneInverseHierarchy;
import vk_gltf_viewer.helpers.concepts;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.optional;
import vk_gltf_viewer.helpers.ranges;
import vk_gltf_viewer.imgui.TaskCollector;
import vk_gltf_viewer.vulkan.imgui.PlatformResource;
import vk_gltf_viewer.vulkan.mipmap;
import vk_gltf_viewer.vulkan.Swapchain;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }
#define INDEX_SEQ(Is, N, ...) [&]<auto ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })
#ifdef _WIN32
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

[[nodiscard]] glm::mat3x2 getTextureTransform(const fastgltf::TextureTransform &transform) noexcept {
    const float c = std::cos(transform.rotation), s = std::sin(transform.rotation);
    return { // Note: column major. A row in code actually means a column in the matrix.
        transform.uvScale[0] * c, transform.uvScale[0] * -s,
        transform.uvScale[1] * s, transform.uvScale[1] * c,
        transform.uvOffset[0], transform.uvOffset[1],
    };
}

vk_gltf_viewer::MainApp::MainApp()
    : swapchain { gpu, window.getSurface(), getSwapchainExtent(), FRAMES_IN_FLIGHT }
    , sharedData { gpu, swapchain.extent, swapchain.images }
    , frames { ARRAY_OF(2, vulkan::Frame { sharedData }) } {
    const ibl::BrdfmapRenderer brdfmapRenderer { gpu.device, brdfmapImage, {} };
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
        brdfmapRenderer.recordCommands(cb);

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
    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

    gpu.device.updateDescriptorSets({
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<0>({ imageBasedLightingResources.cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<1>({ {}, *imageBasedLightingResources.prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<2>({ {}, *brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
    }, {});
}

void vk_gltf_viewer::MainApp::run() {
    std::array<vk::raii::Fence, FRAMES_IN_FLIGHT> frameInFlightFences = ARRAY_OF(FRAMES_IN_FLIGHT, vk::raii::Fence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } });
    
    // When using multiple frames in flight, updating resources in a frame while it’s still being used by the GPU can
    // lead to data hazards. Since resource updates occur when one of the frames is fenced, that frame can be updated
    // safely, but the others cannot.
    // One way to handle this is by storing update tasks for the other frames and executing them once the target frame
    // becomes idle. This approach is especially efficient for two frames in flight, as it requires only a single task
    // vector to store updates for the “another” frame.
    static_assert(FRAMES_IN_FLIGHT == 2, "Frames ≥ 3 needs different update deferring mechanism.");
    std::vector<std::function<void(vulkan::Frame&)>> deferredFrameUpdateTasks;

    std::array<bool, FRAMES_IN_FLIGHT> regenerateDrawCommands{};

    // Currently frame feedback result contains which node is in hovered state, which is only valid
    // with the asset that is used for hovering test. Therefore, if asset may changed, the result is
	// being invalidated. This booleans indicate whether the frame feedback result is valid or not.
    std::array<bool, FRAMES_IN_FLIGHT> frameFeedbackResultValid{};

    // TODO: we need more general mechanism to upload the GPU buffer data in shared data. This is just a stopgap solution
    //  for current KHR_materials_variants implementation.
    const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
    const auto [sharedDataUpdateCommandBuffer] = vku::allocateCommandBuffers<1>(*gpu.device, *graphicsCommandPool);

    for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT) {
        bool hasUpdateData = false;

        std::queue<control::Task> tasks;

        std::vector<std::size_t> transformedNodes;

        // Collect task from animation system.
        if (gltf) {
            std::vector<std::size_t> morphedNodes;
            for (const auto &[animation, enabled] : std::views::zip(gltf->animations, *gltf->animationEnabled)) {
                if (!enabled) continue;
                animation.update(glfwGetTime(), transformedNodes, morphedNodes, gltf->assetExternalBuffers);
            }

            for (std::size_t nodeIndex : morphedNodes) {
                const std::size_t targetWeightCount = getTargetWeightCount(gltf->asset.nodes[nodeIndex], gltf->asset);
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
            if (gltf) {
                imguiTaskCollector.assetInspector(gltf->asset, gltf->directory);
                imguiTaskCollector.assetTextures(gltf->asset, sharedData.gltfAsset->imGuiColorSpaceAndUsageCorrectedTextures, gltf->textureUsages);
                imguiTaskCollector.materialEditor(gltf->asset, sharedData.gltfAsset->imGuiColorSpaceAndUsageCorrectedTextures);
                if (!gltf->asset.materialVariants.empty()) {
                    imguiTaskCollector.materialVariants(gltf->asset);
                }
                imguiTaskCollector.sceneHierarchy(gltf->asset, gltf->sceneIndex, gltf->nodeVisibilities, gltf->hoveringNode, gltf->selectedNodes);
                imguiTaskCollector.nodeInspector(gltf->asset, gltf->animations, *gltf->animationEnabled, gltf->selectedNodes);

                if (!gltf->asset.animations.empty()) {
                    imguiTaskCollector.animations(gltf->asset, gltf->animationEnabled);
                }
            }
            if (const auto &iblInfo = appState.imageBasedLightingProperties) {
                imguiTaskCollector.imageBasedLighting(*iblInfo, vku::toUint64(skyboxResources->imGuiEqmapTextureDescriptorSet));
            }
            imguiTaskCollector.background(appState.canSelectSkyboxBackground, appState.background);
            imguiTaskCollector.inputControl(appState.automaticNearFarPlaneAdjustment, appState.hoveringNodeOutline, appState.selectedNodeOutline, gpu.supportShaderStencilExport);
            if (gltf) {
                imguiTaskCollector.imguizmo(gltf->asset, gltf->selectedNodes, gltf->nodeWorldTransforms, appState.imGuizmoOperation, gltf->animations, *gltf->animationEnabled);
            }
            else {
                imguiTaskCollector.imguizmo();
            }

            if (drawSelectionRectangle) {
                const glm::dvec2 cursorPos = window.getCursorPos();
                ImRect region {
                    ImVec2 { static_cast<float>(lastMouseDownPosition->x), static_cast<float>(lastMouseDownPosition->y) },
                    ImVec2 { static_cast<float>(cursorPos.x), static_cast<float>(cursorPos.y) },
                };
                if (region.Min.x > region.Max.x) {
                    std::swap(region.Min.x, region.Max.x);
                }
                if (region.Min.y > region.Max.y) {
                    std::swap(region.Min.y, region.Max.y);
                }

                ImGui::GetBackgroundDrawList()->AddRectFilled(region.Min, region.Max, ImGui::GetColorU32({ 1.f, 1.f, 1.f, 0.2f }));
                ImGui::GetBackgroundDrawList()->AddRect(region.Min, region.Max, ImGui::GetColorU32({ 1.f, 1.f, 1.f, 1.f }));
            }
        }

        vulkan::Frame &frame = frames[frameIndex];
        vk::Fence inFlightFence = frameInFlightFences[frameIndex];

        // Wait for previous frame execution to end.
        std::ignore = gpu.device.waitForFences(inFlightFence, true, ~0ULL);

        for (const auto &task : deferredFrameUpdateTasks) {
            task(frame);
        }
        deferredFrameUpdateTasks.clear();

        graphicsCommandPool.reset();
        sharedDataUpdateCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        // Process the collected tasks.
        for (; !tasks.empty(); tasks.pop()) {
            visit(multilambda {
                [this](const control::task::WindowKey &task) {
                    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureKeyboard) return;

                    if (task.action == GLFW_PRESS && gltf && !gltf->selectedNodes.empty()) {
                        switch (task.key) {
                            case GLFW_KEY_T:
                                appState.imGuizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
                                break;
                            case GLFW_KEY_R:
                                appState.imGuizmoOperation = ImGuizmo::OPERATION::ROTATE;
                                break;
                            case GLFW_KEY_S:
                                appState.imGuizmoOperation = ImGuizmo::OPERATION::SCALE;
                                break;
                        }
                    }
                },
                [this](const control::task::WindowCursorPos &task) {
                    if (lastMouseDownPosition && distance2(*lastMouseDownPosition, task.position) >= 4.0) {
                        drawSelectionRectangle = true;
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
                    else if (leftMouseButtonPressed && gltf && !selectionRectanglePopped) {
                        if (gltf->hoveringNode) {
                            if (ImGui::GetIO().KeyCtrl) {
                                // Toggle the hovering node's selection.
                                if (auto it = gltf->selectedNodes.find(*gltf->hoveringNode); it != gltf->selectedNodes.end()) {
                                    gltf->selectedNodes.erase(it);
                                }
                                else {
                                    gltf->selectedNodes.emplace_hint(it, *gltf->hoveringNode);
                                }
                                tasks.emplace(std::in_place_type<control::task::NodeSelectionChanged>);
                            }
                            else if (gltf->selectedNodes.size() != 1 || (*gltf->hoveringNode != *gltf->selectedNodes.begin())) {
                                // Unless there's only 1 selected node and is the same as the hovering node, change selection
                                // to the hovering node.
                                gltf->selectedNodes = { *gltf->hoveringNode };
                                tasks.emplace(std::in_place_type<control::task::NodeSelectionChanged>);
                            }
                            global::shouldNodeInSceneHierarchyScrolledToBeVisible = true;
                        }
                        else {
                            gltf->selectedNodes.clear();
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

                    const float factor = std::powf(1.01f, -scale);
                    const glm::vec3 displacementToTarget = global::camera.direction * global::camera.targetDistance;
                    global::camera.targetDistance *= factor;
                    global::camera.position += (1.f - factor) * displacementToTarget;
                },
                [this](const control::task::WindowTrackpadRotate &task) {
                    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

                    // Rotate the camera around the Y-axis lied on the target point.
                    const glm::vec3 target = global::camera.position + global::camera.direction * global::camera.targetDistance;
                    const glm::mat4 rotation = rotate(-glm::radians<float>(task.angle), glm::vec3 { 0.f, 1.f, 0.f });
                    global::camera.direction = glm::mat3 { rotation } * global::camera.direction;
                    global::camera.position = target - global::camera.direction * global::camera.targetDistance;
                },
                [&](const control::task::WindowDrop &task) {
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
                [this](const control::task::ChangePassthruRect &task) {
                    global::camera.aspectRatio = task.newRect.GetWidth() / task.newRect.GetHeight();
                    passthruRect = task.newRect;
                },
                [&](const control::task::LoadGltf &task) {
                    loadGltf(task.path);
                    regenerateDrawCommands.fill(true);
                    frameFeedbackResultValid.fill(false);
                },
                [this](control::task::CloseGltf) {
                    closeGltf();
                },
                [this](const control::task::LoadEqmap &task) {
                    loadEqmap(task.path);
                },
                [&](control::task::ChangeScene task) {
                    gltf->setScene(task.newSceneIndex);

                    auto nodeWorldTransformUpdateTask = [this, sceneIndex = task.newSceneIndex](vulkan::Frame &frame) {
                        if (frame.gltfAsset->instancedNodeWorldTransformBuffer) {
                            frame.gltfAsset->instancedNodeWorldTransformBuffer->update(
                                gltf->asset.scenes[sceneIndex], gltf->nodeWorldTransforms, gltf->assetExternalBuffers);
                        }
                        frame.gltfAsset->nodeBuffer.update(gltf->asset.scenes[sceneIndex], gltf->nodeWorldTransforms);
                    };
                    nodeWorldTransformUpdateTask(frame);
                    deferredFrameUpdateTasks.push_back(std::move(nodeWorldTransformUpdateTask));

                    // Adjust the camera based on the scene enclosing sphere.
                    const auto &[center, radius] = gltf->sceneMiniball.get();
                    const float distance = radius / std::sin(global::camera.fov / 2.f);
                    global::camera.position = glm::make_vec3(center.data()) - distance * normalize(global::camera.direction);
                    global::camera.zMin = distance - radius;
                    global::camera.zMax = distance + radius;
                    global::camera.targetDistance = distance;

                    regenerateDrawCommands.fill(true);
                },
                [&](control::task::NodeVisibilityChanged task) {
                    // TODO: instead of calculate all draw commands, update only changed stuffs based on task.nodeIndex.
                    regenerateDrawCommands.fill(true);
                },
                [this](control::task::NodeSelectionChanged) {
                    assert(gltf);

                    // If selected nodes have a single material, show it in the Material Editor window.
                    std::optional<std::size_t> uniqueMaterialIndex = std::nullopt;
                    for (std::size_t nodeIndex : gltf->selectedNodes) {
                        const auto &meshIndex = gltf->asset.nodes[nodeIndex].meshIndex;
                        if (!meshIndex) continue;

                        for (const fastgltf::Primitive &primitive : gltf->asset.meshes[*meshIndex].primitives) {
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
                        control::ImGuiTaskCollector::selectedMaterialIndex = *uniqueMaterialIndex;
                    }
                },
                [this](const control::task::HoverNodeFromGui &task) {
                    assert(gltf);
                    gltf->hoveringNode.emplace(task.nodeIndex);
                },
                [&](const control::task::NodeLocalTransformChanged &task) {
                    transformedNodes.push_back(task.nodeIndex);
                },
                [&](const control::task::NodeWorldTransformChanged &task) {
                    auto updateNodeTransformTask = [this, nodeIndex = task.nodeIndex](vulkan::Frame &frame) {
                        if (frame.gltfAsset->instancedNodeWorldTransformBuffer) {
                            frame.gltfAsset->instancedNodeWorldTransformBuffer->update(
                                nodeIndex, gltf->nodeWorldTransforms[nodeIndex], gltf->assetExternalBuffers);
                        }
                        frame.gltfAsset->nodeBuffer.update(nodeIndex, gltf->nodeWorldTransforms[nodeIndex]);
                    };
                    updateNodeTransformTask(frame);
                    deferredFrameUpdateTasks.push_back(std::move(updateNodeTransformTask));

                    gltf->sceneMiniball.invalidate();
                },
                [&](const control::task::MaterialPropertyChanged &task) {
                    const fastgltf::Material &changedMaterial = gltf->asset.materials[task.materialIndex];
                    switch (task.property) {
                        using Property = control::task::MaterialPropertyChanged::Property;
                        case Property::AlphaMode:
                        case Property::Unlit:
                        case Property::DoubleSided:
                        case Property::BaseColorTextureTransformEnabled:
                        case Property::EmissiveTextureTransformEnabled:
                        case Property::MetallicRoughnessTextureTransformEnabled:
                        case Property::NormalTextureTransformEnabled:
                        case Property::OcclusionTextureTransformEnabled:
                            regenerateDrawCommands.fill(true);
                            break;
                        case Property::AlphaCutoff:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::alphaCutOff>(
                                task.materialIndex,
                                changedMaterial.alphaCutoff,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::BaseColorFactor:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::baseColorFactor>(
                                task.materialIndex,
                                glm::make_vec4(changedMaterial.pbrData.baseColorFactor.data()),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::BaseColorTextureTransform:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::baseColorTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(*changedMaterial.pbrData.baseColorTexture->transform),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::EmissiveStrength: {
                            const auto it = gltf->bloomMaterials.find(task.materialIndex);
                            const bool useBloom = gltf->asset.materials[task.materialIndex].emissiveStrength > 1.f;

                            // Material emissive strength is changed to 1.
                            if (it != gltf->bloomMaterials.end() && !useBloom) {
                                gltf->bloomMaterials.erase(it);
                                regenerateDrawCommands.fill(true);
                            }
                            // Material emissive strength is changed from 1.
                            else if (it == gltf->bloomMaterials.end() && useBloom) {
                                gltf->bloomMaterials.emplace_hint(it, task.materialIndex);
                                regenerateDrawCommands.fill(true);
                            }
                            [[fallthrough]]; // materialBuffer also needs to be updated.
                        }
                        case Property::Emissive:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::emissive>(
                                task.materialIndex,
                                changedMaterial.emissiveStrength * glm::make_vec3(changedMaterial.emissiveFactor.data()),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::EmissiveTextureTransform:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::emissiveTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(*changedMaterial.emissiveTexture->transform),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::MetallicFactor:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::metallicFactor>(
                                task.materialIndex,
                                changedMaterial.pbrData.metallicFactor,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::MetallicRoughnessTextureTransform:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::metallicRoughnessTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(*changedMaterial.pbrData.metallicRoughnessTexture->transform),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::NormalScale:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::normalScale>(
                                task.materialIndex,
                                changedMaterial.normalTexture->scale,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::NormalTextureTransform:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::normalTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(*changedMaterial.normalTexture->transform),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::OcclusionStrength:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::occlusionStrength>(
                                task.materialIndex,
                                changedMaterial.occlusionTexture->strength,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::OcclusionTextureTransform:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::occlusionTextureTransform>(
                                task.materialIndex,
                                getTextureTransform(*changedMaterial.occlusionTexture->transform),
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::RoughnessFactor:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::roughnessFactor>(
                                task.materialIndex,
                                changedMaterial.pbrData.roughnessFactor,
                                sharedDataUpdateCommandBuffer);
                            break;
                        case Property::Ior:
                            hasUpdateData |= sharedData.gltfAsset->materialBuffer.update<&vulkan::shader_type::Material::ior>(
                                task.materialIndex,
                                changedMaterial.ior,
                                sharedDataUpdateCommandBuffer);
                            break;
                    }
                },
                [&](const control::task::SelectMaterialVariants &task) {
                    assert(gltf && "Synchronization error: gltf is unset but material variants are selected.");

                    gpu.device.waitIdle();

                    for (const auto &[pPrimitive, materialIndex] : gltf->materialVariantsMapping.at(task.variantIndex)) {
                        pPrimitive->materialIndex.emplace(materialIndex);
                        hasUpdateData |= sharedData.gltfAsset->primitiveBuffer.updateMaterial(
                            gltf->orderedPrimitives.getIndex(*pPrimitive), static_cast<std::uint32_t>(materialIndex), sharedDataUpdateCommandBuffer);
                    }
                },
                [&](const control::task::MorphTargetWeightChanged &task) {
                    auto updateTargetWeightTask = [this, task](vulkan::Frame &frame) {
                        const std::span targetWeights = getTargetWeights(gltf->asset.nodes[task.nodeIndex], gltf->asset);
                        for (auto weightIndex = task.targetWeightStartIndex; float weight : targetWeights.subspan(task.targetWeightStartIndex, task.targetWeightCount)) {
                            frame.gltfAsset->morphTargetWeightBuffer.value().updateWeight(task.nodeIndex, weightIndex++, weight);
                        }
                    };
                    updateTargetWeightTask(frame);
                    deferredFrameUpdateTasks.push_back(std::move(updateTargetWeightTask));

                    gltf->sceneMiniball.invalidate();
                },
                [&](control::task::BloomModeChanged) {
                    // Primitive rendering pipelines have to be recreated to use shader stencil export or not.
                    regenerateDrawCommands.fill(true);
                },
            }, tasks.front());
        }

        if (!transformedNodes.empty()) {
            std::vector visited(gltf->asset.nodes.size(), false);

            // Remove duplicates in transformedNodes.
            std::ranges::sort(transformedNodes);
            const auto [begin, end] = std::ranges::unique(transformedNodes);
            transformedNodes.erase(begin, end);

            // Sort transformedNodes by their node level in the scene.
            std::ranges::sort(transformedNodes, {}, LIFT(gltf->sceneNodeLevels.operator[]));

            for (std::size_t nodeIndex : transformedNodes) {
                // If node is marked as visited, its world transform is already updated by its ancestor node. Skipping it.
                if (visited[nodeIndex]) continue;

                // TODO.CXX26: std::optional<const fastgltf::math::fmat4x4&> can ditch the unnecessary copying.
                fastgltf::math::fmat4x4 baseMatrix { 1.f };
                if (const auto &parentNodeIndex = gltf->sceneInverseHierarchy->parentNodeIndices[nodeIndex]) {
                    baseMatrix = gltf->nodeWorldTransforms[*parentNodeIndex];
                }
                const fastgltf::math::fmat4x4 nodeWorldTransform = fastgltf::getTransformMatrix(gltf->asset.nodes[nodeIndex], baseMatrix);

                // Update current and descendants world transforms and mark them as visited.
                traverseNode(gltf->asset, nodeIndex, [&](std::size_t nodeIndex, const fastgltf::math::fmat4x4 &worldTransform) noexcept {
                    // If node is already visited, its descendants must be visited too. Continuing traversal is redundant.
                    if (visited[nodeIndex]) {
                        return false;
                    }

                    gltf->nodeWorldTransforms[nodeIndex] = worldTransform;
                    visited[nodeIndex] = true;
                    return true;
                }, nodeWorldTransform);

                // Update GPU side world transform data.
                auto updateNodeTransformTask = [this, nodeIndex](vulkan::Frame &frame) {
                    if (frame.gltfAsset->instancedNodeWorldTransformBuffer) {
                        frame.gltfAsset->instancedNodeWorldTransformBuffer->updateHierarchical(
                            nodeIndex, gltf->nodeWorldTransforms, gltf->assetExternalBuffers);
                    }
                    frame.gltfAsset->nodeBuffer.updateHierarchical(nodeIndex, gltf->nodeWorldTransforms);
                };
                updateNodeTransformTask(frame);
                deferredFrameUpdateTasks.push_back(std::move(updateNodeTransformTask));
            }

            gltf->sceneMiniball.invalidate();
        }

        if (gltf && appState.automaticNearFarPlaneAdjustment) {
            const auto &[center, radius] = gltf->sceneMiniball.get();
            global::camera.tightenNearFar(glm::make_vec3(center.data()), radius);
        }

        if (hasUpdateData) {
            sharedDataUpdateCommandBuffer.end();

            const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
            gpu.queues.graphicsPresent.submit(vk::SubmitInfo { {}, {}, sharedDataUpdateCommandBuffer }, *fence);
            std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling
        }

        // Update frame resources.
        const glm::vec2 framebufferScale = window.getFramebufferSize() / window.getSize();
        const vulkan::Frame::UpdateResult updateResult = frame.update({
            .passthruRect = vk::Rect2D {
                { static_cast<std::int32_t>(framebufferScale.x * passthruRect.Min.x), static_cast<std::int32_t>(framebufferScale.y * passthruRect.Min.y) },
                { static_cast<std::uint32_t>(framebufferScale.x * passthruRect.GetWidth()), static_cast<std::uint32_t>(framebufferScale.y * passthruRect.GetHeight()) },
            },
            .mousePickingInput = [&]() -> std::variant<std::monostate, vk::Offset2D, vk::Rect2D> {
                const glm::dvec2 cursorPos = window.getCursorPos();
                if (drawSelectionRectangle) {
                    ImRect selectionRect {
                        { static_cast<float>(lastMouseDownPosition->x), static_cast<float>(lastMouseDownPosition->y) },
                        { static_cast<float>(cursorPos.x), static_cast<float>(cursorPos.y) },
                    };
                    if (selectionRect.Min.x > selectionRect.Max.x) {
                        std::swap(selectionRect.Min.x, selectionRect.Max.x);
                    }
                    if (selectionRect.Min.y > selectionRect.Max.y) {
                        std::swap(selectionRect.Min.y, selectionRect.Max.y);
                    }

                    selectionRect.ClipWith(passthruRect);

                    return vk::Rect2D {
                        {
                            static_cast<std::int32_t>(framebufferScale.x * (selectionRect.Min.x - passthruRect.Min.x)),
                            static_cast<std::int32_t>(framebufferScale.y * (selectionRect.Min.y - passthruRect.Min.y)),
                        },
                        {
                            static_cast<std::uint32_t>(framebufferScale.x * selectionRect.GetWidth()),
                            static_cast<std::uint32_t>(framebufferScale.y * selectionRect.GetHeight()),
                        },
                    };
                }
                else if (passthruRect.Contains({ static_cast<float>(cursorPos.x), static_cast<float>(cursorPos.y) }) && !ImGui::GetIO().WantCaptureMouse) {
                    // Note: be aware of implicit vk::Offset2D -> vk::Rect2D promotion!
                    return std::variant<std::monostate, vk::Offset2D, vk::Rect2D> {
                        std::in_place_type<vk::Offset2D>,
                        static_cast<std::int32_t>(framebufferScale.x * (cursorPos.x - passthruRect.Min.x)),
                        static_cast<std::int32_t>(framebufferScale.y * (cursorPos.y - passthruRect.Min.y)),
                    };
                }
                else {
                    return std::monostate{};
                }
            }(),
            .gltf = gltf.transform([&](Gltf &gltf) {
                return vulkan::Frame::ExecutionTask::Gltf {
                    .asset = gltf.asset,
                    .orderedPrimitives = gltf.orderedPrimitives,
                    .nodeWorldTransforms = gltf.nodeWorldTransforms,
                    .regenerateDrawCommands = std::exchange(regenerateDrawCommands[frameIndex], false),
                    .nodeVisibilities = gltf.nodeVisibilities.getVisibilities(),
                    .hoveringNode = transform([&](std::size_t index, const AppState::Outline &outline) {
                        return vulkan::Frame::ExecutionTask::Gltf::HoveringNode {
                            index, outline.color, outline.thickness,
                        };
                    }, gltf.hoveringNode, appState.hoveringNodeOutline.to_optional()),
                    .selectedNodes = value_if(!gltf.selectedNodes.empty() && appState.selectedNodeOutline.has_value(), [&]() {
                        return vulkan::Frame::ExecutionTask::Gltf::SelectedNodes {
                            gltf.selectedNodes,
                            appState.selectedNodeOutline->color,
                            appState.selectedNodeOutline->thickness,
                        };
                    }),
                };
            }),
            .solidBackground = appState.background.to_optional(),
        });

		if (frameFeedbackResultValid[frameIndex]) {
            // Feedback the update result into this.
		    if (auto *indices = get_if<std::vector<std::size_t>>(&updateResult.mousePickingResult)) {
		        assert(gltf);
		        if (ImGui::GetIO().KeyCtrl) {
		            gltf->selectedNodes.insert_range(*indices);
		        }
		        else {
		            gltf->selectedNodes = { std::from_range, *indices };
		        }
		    }
		    else if (auto *index = get_if<std::size_t>(&updateResult.mousePickingResult)) {
		        assert(gltf);
                gltf->hoveringNode = *index;
		    }
		    else if (gltf) {
                gltf->hoveringNode.reset();
		    }
		}
        else {
			frameFeedbackResultValid[frameIndex] = true;
        }

        // Acquire the next swapchain image.
        std::uint32_t swapchainImageIndex;
        vk::Semaphore swapchainImageAcquireSemaphore = *swapchain.imageAcquireSemaphores[frameIndex];
        try {
            vk::Result result [[maybe_unused]];
            std::tie(result, swapchainImageIndex) = (*gpu.device).acquireNextImageKHR(
                *swapchain.swapchain, ~0ULL, swapchainImageAcquireSemaphore);

        #if __APPLE__
            // MoltenVK does not allow presenting suboptimal swapchain image.
            // Issue tracked: https://github.com/KhronosGroup/MoltenVK/issues/2542
            if (result == vk::Result::eSuboptimalKHR) {
                throw vk::OutOfDateKHRError { "Suboptimal swapchain" };
            }
        #endif
        }
        catch (const vk::OutOfDateKHRError&) {
            handleSwapchainResize();
        }

        // Execute frame.
        gpu.device.resetFences(inFlightFence);
        vk::Semaphore swapchainImageReadySemaphore = *swapchain.imageReadySemaphores[swapchainImageIndex];
        frame.recordCommandsAndSubmit(swapchainImageIndex, swapchainImageAcquireSemaphore, swapchainImageReadySemaphore, inFlightFence);

        // Present the rendered swapchain image to swapchain.
        try {
            const vk::Result result [[maybe_unused]] = gpu.queues.graphicsPresent.presentKHR({
                swapchainImageReadySemaphore,
                *swapchain.swapchain,
                swapchainImageIndex,
            });

        #if __APPLE__
            if (result == vk::Result::eSuboptimalKHR) {
                // The result codes VK_ERROR_OUT_OF_DATE_KHR and VK_SUBOPTIMAL_KHR have the same meaning when
                // returned by vkQueuePresentKHR as they do when returned by vkAcquireNextImageKHR.
                throw vk::OutOfDateKHRError { "Suboptimal swapchain" };
            }
        #endif
        }
        catch (const vk::OutOfDateKHRError&) {
            handleSwapchainResize();
        }
    }
    gpu.device.waitIdle();
}

vk_gltf_viewer::MainApp::ImGuiContext::ImGuiContext(const control::AppWindow &window, vk::Instance instance, const vulkan::Gpu &gpu) {
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
        .Instance = instance,
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

vk_gltf_viewer::MainApp::Gltf::Gltf(fastgltf::Parser &parser, const std::filesystem::path &path)
    : dataBuffer { get_checked(fastgltf::GltfDataBuffer::FromPath(path)) }
    , directory { path.parent_path() }
    , asset { get_checked(parser.loadGltf(dataBuffer, directory)) }
    , materialVariantsMapping { getMaterialVariantsMapping(asset) }
    , bloomMaterials { [&]() -> std::unordered_set<std::size_t> {
        using namespace std::string_view_literals;
        if (!ranges::one_of("KHR_materials_emissive_strength"sv, asset.extensionsUsed)) {
            // It is guaranteed that all material emissive strength values are 1.0.
            return {};
        }

        return {
            std::from_range,
            ranges::views::enumerate(asset.materials)
                | std::views::filter(decomposer([](auto, const fastgltf::Material &material) {
                    return material.emissiveStrength > 1.f;
                }))
                | std::views::keys,
        };
    }() }
    , orderedPrimitives { asset }
    , animations { std::from_range, asset.animations | std::views::transform([&](const fastgltf::Animation &animation) {
        return gltf::Animation { asset, animation, assetExternalBuffers };
    }) }
    , animationEnabled { std::make_shared<std::vector<bool>>(asset.animations.size(), false) }
    , sceneIndex { asset.defaultScene.value_or(0) }
    , nodeWorldTransforms { asset, asset.scenes[sceneIndex] }
    , sceneInverseHierarchy { std::make_shared<gltf::ds::SceneInverseHierarchy>(asset, asset.scenes[sceneIndex]) }
    , sceneNodeLevels { asset, asset.scenes[sceneIndex] }
    , nodeVisibilities { asset, asset.scenes[sceneIndex], sceneInverseHierarchy }
    , sceneMiniball { [this]() {
        return gltf::algorithm::getMiniball(asset, asset.scenes[sceneIndex], nodeWorldTransforms, assetExternalBuffers);
    } } { }

void vk_gltf_viewer::MainApp::Gltf::setScene(std::size_t sceneIndex) {
    this->sceneIndex = sceneIndex;
    nodeWorldTransforms.update(asset.scenes[sceneIndex]);
    sceneInverseHierarchy = std::make_shared<gltf::ds::SceneInverseHierarchy>(asset, asset.scenes[sceneIndex]);
    sceneNodeLevels = { asset, asset.scenes[sceneIndex] };
    sceneMiniball.invalidate();
    nodeVisibilities.setScene(asset.scenes[sceneIndex], sceneInverseHierarchy);
    selectedNodes.clear();
    hoveringNode.reset();
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
        ibl::BrdfmapRenderer::requiredResultImageUsageFlags | vk::ImageUsageFlagBits::eSampled,
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

    // Change window title.
    window.setTitle(PATH_C_STR(path.filename()));

    // Update AppState.
    appState.pushRecentGltfPath(path);

    global::bloom.set_active(!gltf->bloomMaterials.empty());

    // Adjust the camera based on the scene enclosing sphere.
    const auto &[center, radius] = gltf->sceneMiniball.get();
    const float distance = radius / std::sin(global::camera.fov / 2.f);
    global::camera.position = glm::make_vec3(center.data()) - distance * normalize(global::camera.direction);
    global::camera.zMin = distance - radius;
    global::camera.zMax = distance + radius;
    global::camera.targetDistance = distance;

    control::ImGuiTaskCollector::selectedMaterialIndex.reset();
}

void vk_gltf_viewer::MainApp::closeGltf() {
    gpu.device.waitIdle();
    for (vulkan::Frame &frame : frames) {
        frame.gltfAsset.reset();
    }
    sharedData.gltfAsset.reset();
    gltf.reset();

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
        cubemap::CubemapComputer::requiredCubemapImageUsageFlags
            | cubemap::SubgroupMipmapComputer::requiredImageUsageFlags
            | ibl::PrefilteredmapComputer::requiredCubemapImageUsageFlags
            | vk::ImageUsageFlagBits::eSampled,
    } };

    const vk::raii::Sampler eqmapSampler { gpu.device, vk::SamplerCreateInfo { {}, vk::Filter::eLinear, vk::Filter::eLinear }.setMaxLod(vk::LodClampNone) };

    const cubemap::CubemapComputer cubemapComputer { gpu.device, eqmapImage, eqmapSampler, cubemapImage };
    const cubemap::SubgroupMipmapComputer subgroupMipmapComputer { gpu.device, cubemapImage, {
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
        ibl::PrefilteredmapComputer::requiredPrefilteredmapImageUsageFlags | vk::ImageUsageFlagBits::eSampled,
    } };
    vku::MappedBuffer sphericalHarmonicsBuffer {
        gpu.allocator,
        vk::BufferCreateInfo {
            {},
            ibl::SphericalHarmonicCoefficientComputer::requiredResultBufferSize,
            ibl::SphericalHarmonicCoefficientComputer::requiredResultBufferUsageFlags | vk::BufferUsageFlagBits::eUniformBuffer,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferDevice,
        },
    };

    const ibl::SphericalHarmonicCoefficientComputer sphericalHarmonicCoefficientComputer { gpu.device, gpu.allocator, cubemapImage, sphericalHarmonicsBuffer, {
        .sampleMipLevel = 0,
        .specializationConstants = {
            .subgroupSize = gpu.subgroupSize,
        },
    } };
    const ibl::PrefilteredmapComputer prefilteredmapComputer { gpu.device, cubemapImage, prefilteredmapImage, {
        .useShaderImageLoadStoreLod = gpu.supportShaderImageLoadStoreLod,
        .specializationConstants = {
            .samples = 1024,
        },
    } };

    // Generate Tone-mapped cubemap.
    const vulkan::rp::CubemapToneMapping cubemapToneMappingRenderPass { gpu.device };
    const vulkan::CubemapToneMappingRenderer cubemapToneMappingRenderer { gpu, cubemapToneMappingRenderPass };

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
                cubemapComputer.recordCommands(cb);

                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                    {},
                    // Ensure eqmap to cubemap projection finish before generating mipmaps.
                    vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                    {}, {});

                // Generate cubemapImage mipmaps.
                subgroupMipmapComputer.recordCommands(cb);

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
                prefilteredmapComputer.recordCommands(cb);

                // Reduce spherical harmonic coefficients.
                sphericalHarmonicCoefficientComputer.recordCommands(cb);

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

                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *cubemapToneMappingRenderer.pipeline);
                cb.pushDescriptorSetKHR(
                    vk::PipelineBindPoint::eGraphics,
                    *cubemapToneMappingRenderer.pipelineLayout,
                    0, vulkan::CubemapToneMappingRenderer::DescriptorSetLayout::getWriteOne<0>({ {}, *cubemapImageArrayView, vk::ImageLayout::eShaderReadOnlyOptimal }));
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
    appState.canSelectSkyboxBackground = true;
    appState.background.reset();
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
