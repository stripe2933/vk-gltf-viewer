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
import :gltf.AssetGpuTextures;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.optional;
import :helpers.ranges;
import :helpers.tristate;
import :imgui.TaskCollector;
import :io.StbDecoder;
import :vulkan.Frame;
import :vulkan.generator.ImageBasedLightingResourceGenerator;
import :vulkan.generator.MipmappedCubemapGenerator;
import :vulkan.mipmap;
import :vulkan.pipeline.BrdfmapComputer;
import :vulkan.pipeline.CubemapToneMappingRenderer;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [](auto &&x) { return __VA_ARGS__(FWD(x)); }
#ifdef _MSC_VER
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

void checkDataBufferLoadResult(bool result) {
    if (!result) {
        throw std::runtime_error { "Failed to load glTF data into buffer" };
    }
}

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
    io.Fonts->AddFontFromFileTTF(
#ifdef _WIN32
        "C:\\Windows\\Fonts\\arial.ttf",
#elif __APPLE__
        "/Library/Fonts/Arial Unicode.ttf",
#elif __linux__
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
#else
#error "Type your own font file in here!"
#endif
        16.f * io.DisplayFramebufferScale.x, nullptr, ranges.Data);
    io.Fonts->Build();

    ImGui_ImplGlfw_InitForVulkan(window, true);
    const auto colorAttachmentFormats = { gpu.supportSwapchainMutableFormat ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb };
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
    // Booleans that indicates frame at the corresponding index should handle swapchain resizing.
    std::array<bool, std::tuple_size_v<decltype(frames)>> shouldHandleSwapchainResize{};

    float elapsedTime = 0.f;
    std::vector<control::Task> tasks;
    for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); ++frameIndex) {
        // Wait for previous frame execution to end.
        frames[frameIndex % frames.size()].waitForPreviousExecution();

        tasks.clear();

        window.handleEvents(tasks);

        const glm::vec2 framebufferSize = window.getFramebufferSize();
        static vk::Rect2D passthruRect{};
        control::ImGuiTaskCollector { tasks, ImVec2 { framebufferSize.x, framebufferSize.y }, passthruRect }
            .menuBar(appState.getRecentGltfPaths(), appState.getRecentSkyboxPaths())
            .assetInspector(appState.gltfAsset.transform([this](auto &x) {
                return std::forward_as_tuple(x.asset, gltf->directory, x.assetInspectorMaterialIndex, assetTextureDescriptorSets);
            }))
            .sceneHierarchy(appState.gltfAsset.transform([](auto &x) -> std::tuple<fastgltf::Asset&, std::size_t, const std::variant<std::vector<std::optional<bool>>, std::vector<bool>>&, const std::optional<std::size_t>&, const std::unordered_set<std::size_t>&> {
                // TODO: don't know why, but using std::forward_as_tuple will pass the scene index as reference and will
                //  cause a dangling reference. Should be investigated.
                return { x.asset, x.getSceneIndex(), x.nodeVisibilities, x.hoveringNodeIndex, x.selectedNodeIndices };
            }))
            .nodeInspector(appState.gltfAsset.transform([](auto &x) {
                return std::forward_as_tuple(x.asset, x.selectedNodeIndices);
            }))
            .imageBasedLighting(appState.imageBasedLightingProperties.transform([this](const auto &info) {
                return std::forward_as_tuple(info, skyboxResources->imGuiEqmapTextureDescriptorSet);
            }))
            .background(appState.canSelectSkyboxBackground, appState.background)
            .inputControl(appState.camera, appState.automaticNearFarPlaneAdjustment, appState.useFrustumCulling, appState.hoveringNodeOutline, appState.selectedNodeOutline)
            .imguizmo(appState.camera, appState.gltfAsset.and_then([this](auto &x) {
                return value_if(x.selectedNodeIndices.size() == 1, [&]() {
                    return std::tuple<fastgltf::Asset&, std::span<const glm::mat4>, std::size_t, ImGuizmo::OPERATION> {
                        x.asset,
                        gltf->assetSceneGpuBuffers.nodeWorldTransformBuffer.asRange<const glm::mat4>(),
                        *x.selectedNodeIndices.begin(),
                        appState.imGuizmoOperation,
                    };
                });
            }));

        for (const control::Task &task : tasks) {
            visit(multilambda {
                [this](const control::task::ChangePassthruRect &task) {
                    appState.camera.aspectRatio = vku::aspect(task.newRect.extent);
                    passthruRect = task.newRect;
                },
                [&](const control::task::LoadGltf &task) {
                    // TODO: I'm aware that there are more good solutions than waitIdle, but I don't have much time for it
                    //  so I'll just use it for now.
                    gpu.device.waitIdle();

                    gltf.emplace(parser, task.path, gpu);

                    sharedData.updateTextureCount(1 + gltf->asset.textures.size());

                    std::vector<vk::DescriptorImageInfo> imageInfos;
                    imageInfos.reserve(1 + gltf->asset.textures.size());
                    imageInfos.emplace_back(*sharedData.singleTexelSampler, *gpuFallbackTexture.imageView, vk::ImageLayout::eShaderReadOnlyOptimal);
                    imageInfos.append_range(gltf->asset.textures | std::views::transform([this](const fastgltf::Texture &texture) {
                        return vk::DescriptorImageInfo {
                            to_optional(texture.samplerIndex)
                                .transform([this](std::size_t samplerIndex) { return *gltf->assetGpuTextures.samplers[samplerIndex]; })
                                .value_or(*gpuFallbackTexture.sampler),
                            *gltf->assetGpuTextures.imageViews.at(gltf::AssetGpuTextures::getPreferredImageIndex(texture)),
                            vk::ImageLayout::eShaderReadOnlyOptimal,
                        };
                    }));
                    gpu.device.updateDescriptorSets({
                        sharedData.assetDescriptorSet.getWrite<0>(imageInfos),
                        sharedData.assetDescriptorSet.getWriteOne<1>({ gltf->assetGpuBuffers.materialBuffer, 0, vk::WholeSize }),
                        sharedData.sceneDescriptorSet.getWriteOne<0>({ gltf->assetSceneGpuBuffers.primitiveBuffer, 0, vk::WholeSize }),
                        sharedData.sceneDescriptorSet.getWriteOne<1>({ gltf->assetSceneGpuBuffers.nodeWorldTransformBuffer, 0, vk::WholeSize }),
                    }, {});

                    // TODO: due to the ImGui's gamma correction issue, base color/emissive texture is rendered darker than it should be.
                    assetTextureDescriptorSets
                        = gltf->asset.textures
                        | std::views::transform([this](const fastgltf::Texture &texture) -> vk::DescriptorSet {
                            return ImGui_ImplVulkan_AddTexture(
                                to_optional(texture.samplerIndex)
                                    .transform([this](std::size_t samplerIndex) { return *gltf->assetGpuTextures.samplers[samplerIndex]; })
                                    .value_or(*gpuFallbackTexture.sampler),
                                *gltf->assetGpuTextures.imageViews.at(gltf::AssetGpuTextures::getPreferredImageIndex(texture)),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                        })
                        | std::ranges::to<std::vector>();

                    // Update AppState.
                    appState.gltfAsset.emplace(gltf->asset);
                    appState.pushRecentGltfPath(task.path);

                    // Adjust the camera based on the scene enclosing sphere.
                    const auto &[center, radius] = gltf->assetSceneMiniball;
                    const float distance = radius / std::sin(appState.camera.fov / 2.f);
                    appState.camera.position = center - glm::dvec3 { distance * normalize(appState.camera.direction) };
                    appState.camera.zMin = distance - radius;
                    appState.camera.zMax = distance + radius;
                    appState.camera.targetDistance = distance;
                },
                [&](control::task::CloseGltf) {
                    gltf.reset();

                    // Update AppState.
                    appState.gltfAsset.reset();
                },
                [&](const control::task::LoadEqmap &task) {
                    processEqmapChange(task.path);

                    // Update AppState.
                    appState.pushRecentSkyboxPath(task.path);
                },
                [this](control::task::ChangeScene task) {
                    // TODO: I'm aware that there are more good solutions than waitIdle, but I don't have much time for it
                    //  so I'll just use it for now.
                    gpu.device.waitIdle();

                    gltf->setScene(task.newSceneIndex);

                    gpu.device.updateDescriptorSets({
                        sharedData.sceneDescriptorSet.getWriteOne<0>({ gltf->assetSceneGpuBuffers.primitiveBuffer, 0, vk::WholeSize }),
                        sharedData.sceneDescriptorSet.getWriteOne<1>({ gltf->assetSceneGpuBuffers.nodeWorldTransformBuffer, 0, vk::WholeSize }),
                    }, {});

                    // Update AppState.
                    appState.gltfAsset->setScene(task.newSceneIndex);

                    // Adjust the camera based on the scene enclosing sphere.
                    const auto &[center, radius] = gltf->assetSceneMiniball;
                    const float distance = radius / std::sin(appState.camera.fov / 2.f);
                    appState.camera.position = center - glm::dvec3 { distance * normalize(appState.camera.direction) };
                    appState.camera.zMin = distance - radius;
                    appState.camera.zMax = distance + radius;
                    appState.camera.targetDistance = distance;
                },
                [this](control::task::ChangeNodeVisibilityType) {
                    visit(multilambda {
                        [this](std::span<const std::optional<bool>> visibilities) {
                            appState.gltfAsset->nodeVisibilities.emplace<std::vector<bool>>(
                                std::from_range,
                                visibilities | std::views::transform([](std::optional<bool> visibility) {
                                    return visibility.value_or(true);
                                }));
                        },
                        [this](const std::vector<bool> &visibilities) {
                            appState.gltfAsset->nodeVisibilities.emplace<std::vector<std::optional<bool>>>(visibilities.size(), true);
                        },
                    }, appState.gltfAsset->nodeVisibilities);
                },
                [this](control::task::ChangeNodeVisibility task) {
                    visit(multilambda {
                        [&](std::span<std::optional<bool>> visibilities) {
                            if (auto &visibility = visibilities[task.nodeIndex]; visibility) {
                                *visibility = !*visibility;
                            }
                            else {
                                visibility.emplace(true);
                            }

                            tristate::propagateTopDown(
                                [&](auto i) -> decltype(auto) { return gltf->asset.nodes[i].children; },
                                task.nodeIndex, visibilities);
                            tristate::propagateBottomUp(
                                [&](auto i) { return appState.gltfAsset->getParentNodeIndex(i).value_or(i); },
                                [&](auto i) -> decltype(auto) { return gltf->asset.nodes[i].children; },
                                task.nodeIndex, visibilities);
                        },
                        [&](std::vector<bool> &visibilities) {
                            visibilities[task.nodeIndex].flip();
                        },
                    }, appState.gltfAsset->nodeVisibilities);
                },
                [this](const control::task::SelectNodeFromSceneHierarchy &task) {
                    if (!task.combine) {
                        appState.gltfAsset->selectedNodeIndices.clear();
                    }
                    appState.gltfAsset->selectedNodeIndices.emplace(task.nodeIndex);
                },
                [this](const control::task::HoverNodeFromSceneHierarchy &task) {
                    appState.gltfAsset->hoveringNodeIndex.emplace(task.nodeIndex);
                },
                [this](const control::task::ChangeNodeLocalTransform &task) {
                    // Update AssetSceneGpuBuffers::nodeWorldTransformBuffer.
                    const std::span nodeWorldTransforms = gltf->assetSceneGpuBuffers.nodeWorldTransformBuffer.asRange<glm::mat4>();

                    // FIXME: due to the Clang 18's explicit object parameter bug, const fastgltf::Asset& and std::span<glm::mat4> are passed (but it is unnecessary). Remove the parameter when fixed.
                    const auto applyNodeLocalTransformChangeRecursive
                        = [](this auto self, const fastgltf::Asset &asset, std::span<glm::mat4> nodeWorldTransforms, std::size_t nodeIndex, const glm::mat4 &parentNodeWorldTransform = { 1.f }) -> void {
                            const fastgltf::Node &node = asset.nodes[nodeIndex];
                            nodeWorldTransforms[nodeIndex] = parentNodeWorldTransform * visit(LIFT(fastgltf::toMatrix), node.transform);

                            for (std::size_t childNodeIndex : node.children) {
                                self(asset, nodeWorldTransforms, childNodeIndex, nodeWorldTransforms[nodeIndex]);
                            }
                    };

                    // Start from the current selected node, execute applyNodeLocalTransformChangeRecursive with its
                    // parent node's world transform (it must be identity matrix if selected node is root node).
                    const glm::mat4 parentNodeWorldTransform
                        = appState.gltfAsset->getParentNodeIndex(task.nodeIndex)
                        .transform([&](std::size_t parentNodeIndex) {
                            return nodeWorldTransforms[parentNodeIndex];
                        })
                        .value_or(glm::mat4 { 1.f });
                    applyNodeLocalTransformChangeRecursive(gltf->asset, nodeWorldTransforms, task.nodeIndex, parentNodeWorldTransform);

                    // Scene enclosing sphere would be changed. Adjust the camera's near/far plane if necessary.
                    if (appState.automaticNearFarPlaneAdjustment) {
                        const auto &[center, radius]
                            = (gltf->assetSceneMiniball = gltf::algorithm::getMiniball(gltf->asset, gltf->scene, gltf->assetSceneGpuBuffers.nodeWorldTransformBuffer.asRange<const glm::mat4>()));
                        appState.camera.tightenNearFar(center, radius);
                    }
                },
                [this](control::task::TightenNearFarPlane) {
                    if (gltf) {
                        const auto &[center, radius] = gltf->assetSceneMiniball;
                        appState.camera.tightenNearFar(center, radius);
                    }
                },
                [this](control::task::ChangeCameraView) {
                    if (appState.automaticNearFarPlaneAdjustment && gltf) {
                        // Tighten near/far plane based on the scene enclosing sphere.
                        const auto &[center, radius] = gltf->assetSceneMiniball;
                        appState.camera.tightenNearFar(center, radius);
                    }
                },
            }, task);
        }

        const vulkan::Frame::ExecutionTask task {
            .passthruRect = passthruRect,
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
                    static_cast<std::int32_t>(framebufferCursorPosition.x) - passthruRect.offset.x,
                    static_cast<std::int32_t>(framebufferCursorPosition.y) - passthruRect.offset.y
                };
                return value_if(0 <= offset.x && offset.x < passthruRect.extent.width && 0 <= offset.y && offset.y < passthruRect.extent.height, offset);
            }),
            .hoveringNodeOutline = appState.hoveringNodeOutline.to_optional(),
            .selectedNodeOutline = appState.selectedNodeOutline.to_optional(),
            .gltf = gltf.transform([&](Gltf &gltf) {
                assert(appState.gltfAsset && "Synchronization error: gltfAsset is not set in AppState.");
                return vulkan::Frame::ExecutionTask::Gltf {
                    .asset = gltf.asset,
                    .indexBuffers = gltf.assetGpuBuffers.indexBuffers,
                    .assetSceneGpuBuffers = gltf.assetSceneGpuBuffers,
                    .hoveringNodeIndex = appState.gltfAsset->hoveringNodeIndex,
                    .selectedNodeIndices = appState.gltfAsset->selectedNodeIndices,
                    .renderingNodeIndices = appState.gltfAsset->getVisibleNodeIndices(),
                };
            }),
            .solidBackground = appState.background.to_optional(),
            .handleSwapchainResize = std::exchange(shouldHandleSwapchainResize[frameIndex % frames.size()], false),
        };

        const vulkan::Frame::UpdateResult updateResult = frames[frameIndex % frames.size()].update(task);
        // Updating hovering node index should be only done if mouse cursor is inside the passthru rect (otherwise, it
        // should be manipulated by ImGui scene hierarchy tree).
        if (task.cursorPosFromPassthruRectTopLeft && appState.gltfAsset) {
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

vk_gltf_viewer::MainApp::Gltf::Gltf(
    fastgltf::Parser &parser,
    const std::filesystem::path &path,
    const vulkan::Gpu &_gpu [[clang::lifetimebound]],
    fastgltf::GltfDataBuffer dataBuffer
) : directory { path.parent_path() },
    assetExpected { (checkDataBufferLoadResult(dataBuffer.loadFromFile(path)), parser.loadGltf(&dataBuffer, directory)) },
    gpu { _gpu },
    assetExternalBuffers { std::make_unique<gltf::AssetExternalBuffers>(asset, directory) },
    assetGpuBuffers { asset, *assetExternalBuffers, gpu },
    assetGpuTextures { asset, directory, *assetExternalBuffers, gpu },
    assetSceneGpuBuffers { asset, assetGpuBuffers, scene, gpu } {
    assetExternalBuffers.reset(); // Drop the intermediate result that are not used in rendering.
}

void vk_gltf_viewer::MainApp::Gltf::setScene(std::size_t sceneIndex) {
    scene = asset.scenes[sceneIndex];
    assetSceneGpuBuffers = { asset, assetGpuBuffers, scene, gpu };
    assetSceneMiniball = gltf::algorithm::getMiniball(asset, scene, assetSceneGpuBuffers.nodeWorldTransformBuffer.asRange<const glm::mat4>());
}

auto vk_gltf_viewer::MainApp::createInstance() const -> vk::raii::Instance {
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
            + /*static_cast<std::uint32_t>(gltf->asset.textures.size())*/ 512 /* material textures */, // TODO: need proper texture count.
        vku::unsafeProxy(vk::DescriptorPoolSize {
            vk::DescriptorType::eCombinedImageSampler,
            1 /* Default ImGui rendering */
                + 1 /* reducedEqmapImage texture */
                + /*static_cast<std::uint32_t>(gltf->asset.textures.size())*/ 512 /* material textures */ // TODO: need proper texture count.
        }),
    } };
}

auto vk_gltf_viewer::MainApp::processEqmapChange(
    const std::filesystem::path &eqmapPath
) -> void {
    // Load equirectangular map image and stage it into eqmapImage.
    int width, height;
    if (!stbi_info(PATH_C_STR(eqmapPath), &width, &height, nullptr)) {
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
    const vulkan::CubemapToneMappingRenderer cubemapToneMappingRenderer { gpu.device, cubemapToneMappingRenderPass };

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
                    std::from_range, io::StbDecoder<float>::fromFile(PATH_C_STR(eqmapPath), 4).asSpan(),
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
}