module;

#include <cassert>
#include <GLFW/glfw3.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfThreading.h>
#include <stb_image.h>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :MainApp;

import std;
import imgui.glfw;
import imgui.vulkan;
import ImGuizmo;
import :gltf.AssetExternalBuffers;
import :gltf.AssetGpuTextures;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.optional;
import :helpers.ranges;
import :helpers.tristate;
import :imgui.TaskCollector;
import :vulkan.Frame;
import :vulkan.generator.ImageBasedLightingResourceGenerator;
import :vulkan.generator.MipmappedCubemapGenerator;
import :vulkan.mipmap;
import :vulkan.pipeline.BrdfmapComputer;
import :vulkan.pipeline.CubemapToneMappingRenderer;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }
#ifdef _MSC_VER
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;

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
            }, visit_as<vk::CommandPool>(graphicsCommandPool), gpu.queues.graphicsPresent }));

    std::ignore = gpu.device.waitSemaphores({
        {},
        vku::unsafeProxy(timelineSemaphores | ranges::views::deref | std::ranges::to<std::vector>()),
        finalWaitValues
    }, ~0ULL);

    const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, visit_as<vk::CommandPool>(graphicsCommandPool), gpu.queues.graphicsPresent, [this](vk::CommandBuffer cb) {
        recordSwapchainImageLayoutTransitionCommands(cb);
    }, *fence);
    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

    // Init ImGui.
    ImGui::CheckVersion();
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
    const vk::Format colorAttachmentFormat = gpu.supportSwapchainMutableFormat ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb;
    ImGui_ImplVulkan_InitInfo initInfo {
        .Instance = *instance,
        .PhysicalDevice = *gpu.physicalDevice,
        .Device = *gpu.device,
        .Queue = gpu.queues.graphicsPresent,
        .DescriptorPool = *imGuiDescriptorPool,
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

void vk_gltf_viewer::MainApp::run() {
    vulkan::SharedData sharedData { gpu, swapchainExtent, swapchainImages };
    std::array frames = ARRAY_OF(FRAMES_IN_FLIGHT, vulkan::Frame { gpu, sharedData });

    gpu.device.updateDescriptorSets({
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<0>({ imageBasedLightingResources.cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<1>({ {}, *imageBasedLightingResources.prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
        sharedData.imageBasedLightingDescriptorSet.getWriteOne<2>({ {}, *brdfmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
    }, {});

    // Booleans that indicates frame at the corresponding index should handle swapchain resizing.
    std::array<bool, FRAMES_IN_FLIGHT> shouldHandleSwapchainResize{};

    std::vector<control::Task> tasks;
    for (std::uint64_t frameIndex = 0; !glfwWindowShouldClose(window); frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT) {
        tasks.clear();

        // Collect task from window event (mouse, keyboard, drag and drop, ...).
        window.handleEvents(tasks);

        // Collect task from ImGui (button click, menu selection, ...).
        static vk::Rect2D passthruRect{};
        {
            control::ImGuiTaskCollector imguiTaskCollector {
                tasks,
                ImVec2 { static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height) },
                passthruRect,
            };

            imguiTaskCollector.menuBar(appState.getRecentGltfPaths(), appState.getRecentSkyboxPaths());
            if (auto &gltfAsset = appState.gltfAsset) {
                imguiTaskCollector.assetInspector(gltfAsset->asset, gltf->directory);
                imguiTaskCollector.materialEditor(gltfAsset->asset, gltfAsset->assetInspectorMaterialIndex, assetTextureDescriptorSets);
                imguiTaskCollector.sceneHierarchy(gltfAsset->asset, gltfAsset->getSceneIndex(), gltfAsset->nodeVisibilities, gltfAsset->hoveringNodeIndex, gltfAsset->selectedNodeIndices);
                imguiTaskCollector.nodeInspector(gltfAsset->asset, gltfAsset->selectedNodeIndices);
            }
            if (const auto &iblInfo = appState.imageBasedLightingProperties) {
                imguiTaskCollector.imageBasedLighting(*iblInfo, skyboxResources->imGuiEqmapTextureDescriptorSet);
            }
            imguiTaskCollector.background(appState.canSelectSkyboxBackground, appState.background);
            imguiTaskCollector.inputControl(appState.camera, appState.automaticNearFarPlaneAdjustment, appState.useFrustumCulling, appState.hoveringNodeOutline, appState.selectedNodeOutline);
            if (appState.gltfAsset && appState.gltfAsset->selectedNodeIndices.size() == 1) {
                const std::size_t selectedNodeIndex = *appState.gltfAsset->selectedNodeIndices.begin();
                imguiTaskCollector.imguizmo(appState.camera, gltf->sceneHierarchy.nodeWorldTransforms[selectedNodeIndex], appState.imGuizmoOperation);
            }
            else {
                imguiTaskCollector.imguizmo(appState.camera);
            }
        }

        bool regenerateDrawCommands = false;
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
                        sharedData.assetDescriptorSet.getWriteOne<0>({ gltf->assetGpuBuffers.primitiveBuffer, 0, vk::WholeSize }),
                        sharedData.assetDescriptorSet.getWriteOne<1>({ gltf->assetGpuBuffers.materialBuffer, 0, vk::WholeSize }),
                        sharedData.assetDescriptorSet.getWrite<2>(imageInfos),
                        sharedData.sceneDescriptorSet.getWriteOne<0>({ gltf->sceneGpuBuffers.nodeBuffer, 0, vk::WholeSize }),
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

                    // Change window title.
                    window.setTitle(PATH_C_STR(task.path.filename()));

                    // Update AppState.
                    appState.gltfAsset.emplace(gltf->asset);
                    appState.pushRecentGltfPath(task.path);

                    // Adjust the camera based on the scene enclosing sphere.
                    const auto &[center, radius] = gltf->sceneMiniball;
                    const float distance = radius / std::sin(appState.camera.fov / 2.f);
                    appState.camera.position = glm::make_vec3(center.data()) - glm::dvec3 { distance * normalize(appState.camera.direction) };
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

                    // Update the related descriptor sets.
                    gpu.device.updateDescriptorSets({
                        sharedData.imageBasedLightingDescriptorSet.getWriteOne<0>({ imageBasedLightingResources.cubemapSphericalHarmonicsBuffer, 0, vk::WholeSize }),
                        sharedData.imageBasedLightingDescriptorSet.getWriteOne<1>({ {}, *imageBasedLightingResources.prefilteredmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
                        sharedData.skyboxDescriptorSet.getWriteOne<0>({ {}, *skyboxResources->cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
                    }, {});

                    // Update AppState.
                    appState.pushRecentSkyboxPath(task.path);
                },
                [&](control::task::ChangeScene task) {
                    // TODO: I'm aware that there are more good solutions than waitIdle, but I don't have much time for it
                    //  so I'll just use it for now.
                    gpu.device.waitIdle();

                    gltf->setScene(task.newSceneIndex);

                    gpu.device.updateDescriptorSets(
                        sharedData.sceneDescriptorSet.getWriteOne<0>({ gltf->sceneGpuBuffers.nodeBuffer, 0, vk::WholeSize }),
                        {});

                    // Update AppState.
                    appState.gltfAsset->setScene(task.newSceneIndex);

                    // Adjust the camera based on the scene enclosing sphere.
                    const auto &[center, radius] = gltf->sceneMiniball;
                    const float distance = radius / std::sin(appState.camera.fov / 2.f);
                    appState.camera.position = glm::make_vec3(center.data()) - glm::dvec3 { distance * normalize(appState.camera.direction) };
                    appState.camera.zMin = distance - radius;
                    appState.camera.zMax = distance + radius;
                    appState.camera.targetDistance = distance;
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
                                [&](auto i) { return gltf->sceneHierarchy.getParentNodeIndex(i).value_or(i); },
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
                    fastgltf::math::fmat4x4 nodeWorldTransform = visit(fastgltf::visitor {
                        [](const fastgltf::TRS &trs) { return toMatrix(trs); },
                        [](fastgltf::math::fmat4x4 matrix) { return matrix; }
                    }, gltf->asset.nodes[task.nodeIndex].transform);
                    if (auto parentNodeIndex = gltf->sceneHierarchy.getParentNodeIndex(task.nodeIndex)) {
                        nodeWorldTransform = gltf->sceneHierarchy.nodeWorldTransforms[*parentNodeIndex] * nodeWorldTransform;
                    }

                    // Update the current and its descendant nodes' world transforms in sceneHierarchy.
                    gltf->sceneHierarchy.updateDescendantNodeTransformsFrom(task.nodeIndex, nodeWorldTransform);

                    // Passing sceneHierarchy into sceneGpuBuffers to update GPU mesh node transform buffer.
                    gltf->sceneGpuBuffers.updateMeshNodeTransformsFrom(task.nodeIndex, gltf->sceneHierarchy, gltf->assetExternalBuffers);

                    // Scene enclosing sphere would be changed. Adjust the camera's near/far plane if necessary.
                    if (appState.automaticNearFarPlaneAdjustment) {
                        const auto &[center, radius]
                            = gltf->sceneMiniball
                            = gltf::algorithm::getMiniball(
                                gltf->asset, gltf->scene, [this](std::size_t nodeIndex, std::size_t instanceIndex) {
                                    return cast<double>(gltf->sceneGpuBuffers.getMeshNodeWorldTransform(nodeIndex, instanceIndex));
                                });
                        appState.camera.tightenNearFar(glm::make_vec3(center.data()), radius);
                    }
                },
                [this](control::task::ChangeSelectedNodeWorldTransform) {
                    const std::size_t selectedNodeIndex = *appState.gltfAsset->selectedNodeIndices.begin();
                    const fastgltf::math::fmat4x4 &selectedNodeWorldTransform = gltf->sceneHierarchy.nodeWorldTransforms[selectedNodeIndex];

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
                            if (auto parentNodeIndex = gltf->sceneHierarchy.getParentNodeIndex(selectedNodeIndex)) {
                                transformMatrix = affineInverse(gltf->sceneHierarchy.nodeWorldTransforms[*parentNodeIndex]) * selectedNodeWorldTransform;
                            }
                            else {
                                transformMatrix = selectedNodeWorldTransform;
                            }
                        },
                        [&](fastgltf::TRS &trs) {
                            if (auto parentNodeIndex = gltf->sceneHierarchy.getParentNodeIndex(selectedNodeIndex)) {
                                const fastgltf::math::fmat4x4 transformMatrix = affineInverse(gltf->sceneHierarchy.nodeWorldTransforms[*parentNodeIndex]) * selectedNodeWorldTransform;
                                decomposeTransformMatrix(transformMatrix, trs.scale, trs.rotation, trs.translation);
                            }
                            else {
                                decomposeTransformMatrix(selectedNodeWorldTransform, trs.scale, trs.rotation, trs.translation);
                            }
                        },
                    }, gltf->asset.nodes[selectedNodeIndex].transform);

                    // Update the current and its descendant nodes' world transforms in sceneHierarchy.
                    gltf->sceneHierarchy.updateDescendantNodeTransformsFrom(selectedNodeIndex, selectedNodeWorldTransform);

                    // Passing sceneHierarchy into sceneGpuBuffers to update GPU mesh node transform buffer.
                    gltf->sceneGpuBuffers.updateMeshNodeTransformsFrom(selectedNodeIndex, gltf->sceneHierarchy, gltf->assetExternalBuffers);

                    // Scene enclosing sphere would be changed. Adjust the camera's near/far plane if necessary.
                    if (appState.automaticNearFarPlaneAdjustment) {
                        const auto &[center, radius]
                            = gltf->sceneMiniball
                            = gltf::algorithm::getMiniball(
                                gltf->asset, gltf->scene, [this](std::size_t nodeIndex, std::size_t instanceIndex) {
                                    return cast<double>(gltf->sceneGpuBuffers.getMeshNodeWorldTransform(nodeIndex, instanceIndex));
                                });
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
                    regenerateDrawCommands = true;
                },
            }, task);
        }

        // Wait for previous frame execution to end.
        vulkan::Frame &frame = frames[frameIndex];
        frame.waitForPreviousExecution();

        // Update frame resources.
        const vulkan::Frame::UpdateResult updateResult = frame.update({
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
            .gltf = gltf.transform([&](Gltf &gltf) {
                assert(appState.gltfAsset && "Synchronization error: gltfAsset is not set in AppState.");
                return vulkan::Frame::ExecutionTask::Gltf {
                    .asset = gltf.asset,
                    .assetGpuBuffers = gltf.assetGpuBuffers,
                    .sceneHierarchy = gltf.sceneHierarchy,
                    .sceneGpuBuffers = gltf.sceneGpuBuffers,
                    .renderingNodes = {
                        .indices = appState.gltfAsset->getVisibleNodeIndices(),
                        .shouldRegenerateDrawCommands = regenerateDrawCommands,
                    },
                    .hoveringNode = transform([&](std::uint16_t index, const AppState::Outline &outline) {
                        return vulkan::Frame::ExecutionTask::Gltf::HoveringNode {
                            index, outline.color, outline.thickness, regenerateDrawCommands,
                        };
                    }, appState.gltfAsset->hoveringNodeIndex, appState.hoveringNodeOutline.to_optional()),
                    .selectedNodes = value_if(!appState.gltfAsset->selectedNodeIndices.empty() && appState.selectedNodeOutline.has_value(), [&]() {
                        return vulkan::Frame::ExecutionTask::Gltf::SelectedNodes {
                            appState.gltfAsset->selectedNodeIndices,
                            appState.selectedNodeOutline->color,
                            appState.selectedNodeOutline->thickness,
                            regenerateDrawCommands,
                        };
                    }),
                };
            }),
            .solidBackground = appState.background.to_optional(),
            .handleSwapchainResize = std::exchange(shouldHandleSwapchainResize[frameIndex % frames.size()], false),
        });

        // Feedback the update result into this.
        if (appState.gltfAsset) {
            appState.gltfAsset->hoveringNodeIndex = updateResult.hoveringNodeIndex;
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

vk_gltf_viewer::MainApp::Gltf::Gltf(
    fastgltf::Parser &parser,
    const std::filesystem::path &path,
    const vulkan::Gpu &gpu [[clang::lifetimebound]],
    BS::thread_pool threadPool
) : dataBuffer { get_checked(fastgltf::GltfDataBuffer::FromPath(path)) },
    directory { path.parent_path() },
    asset { get_checked(parser.loadGltf(dataBuffer, directory)) },
    gpu { gpu },
    assetGpuBuffers { asset, gpu, threadPool, assetExternalBuffers },
    assetGpuTextures { asset, directory, gpu, threadPool, assetExternalBuffers },
    sceneGpuBuffers { asset, scene, sceneHierarchy, gpu, assetExternalBuffers },
    sceneMiniball { gltf::algorithm::getMiniball(asset, scene, [this](std::size_t nodeIndex, std::size_t instanceIndex) {
        return cast<double>(sceneGpuBuffers.getMeshNodeWorldTransform(nodeIndex, instanceIndex));
    }) } { }

void vk_gltf_viewer::MainApp::Gltf::setScene(std::size_t sceneIndex) {
    scene = asset.scenes[sceneIndex];
    sceneHierarchy = { asset, scene };
    sceneGpuBuffers = { asset, scene, sceneHierarchy, gpu, assetExternalBuffers };
    sceneMiniball = gltf::algorithm::getMiniball(asset, scene, [this](std::size_t nodeIndex, std::size_t instanceIndex) {
        return cast<double>(sceneGpuBuffers.getMeshNodeWorldTransform(nodeIndex, instanceIndex));
    });
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

vk::raii::SwapchainKHR vk_gltf_viewer::MainApp::createSwapchain(vk::SwapchainKHR oldSwapchain) const {
    const vk::SurfaceKHR surface = window.getSurface();
    const vk::SurfaceCapabilitiesKHR surfaceCapabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);
    const auto viewFormats = { vk::Format::eB8G8R8A8Srgb, vk::Format::eB8G8R8A8Unorm };

    vk::StructureChain createInfo {
        vk::SwapchainCreateInfoKHR{
            gpu.supportSwapchainMutableFormat ? vk::SwapchainCreateFlagBitsKHR::eMutableFormat : vk::SwapchainCreateFlagsKHR{},
            surface,
            std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount),
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
        else if (extension == ".exr") {
            Imf::InputFile file { PATH_C_STR(eqmapPath), static_cast<int>(std::thread::hardware_concurrency()) };

            const Imath::Box2i dw = file.header().dataWindow();
            const vk::Extent2D eqmapExtent {
                static_cast<std::uint32_t>(dw.max.x - dw.min.x + 1),
                static_cast<std::uint32_t>(dw.max.y - dw.min.y + 1),
            };

            vku::MappedBuffer buffer { gpu.allocator, vk::BufferCreateInfo {
                {},
                blockSize(vk::Format::eR32G32B32A32Sfloat) * eqmapExtent.width * eqmapExtent.height,
                vk::BufferUsageFlagBits::eTransferSrc,
            } };
            const std::span data = buffer.asRange<glm::vec4>();

            // Create frame buffers for each channel.
            // Note: Alpha channel will be ignored.
            Imf::FrameBuffer frameBuffer;
            const std::size_t rowBytes = eqmapExtent.width * sizeof(glm::vec4);
            frameBuffer.insert("R", Imf::Slice { Imf::FLOAT, reinterpret_cast<char*>(&data[0].x), sizeof(glm::vec4), rowBytes });
            frameBuffer.insert("G", Imf::Slice { Imf::FLOAT, reinterpret_cast<char*>(&data[0].y), sizeof(glm::vec4), rowBytes });
            frameBuffer.insert("B", Imf::Slice { Imf::FLOAT, reinterpret_cast<char*>(&data[0].z), sizeof(glm::vec4), rowBytes });

            file.readPixels(frameBuffer, dw.min.y, dw.max.y);

            return std::pair { eqmapExtent, std::move(buffer) };
        }

        throw std::runtime_error { "Unknown file format: only HDR and EXR are supported." };
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
    std::variant<vk::CommandPool, vk::raii::CommandPool> computeCommandPool = *transferCommandPool;
    std::variant<vk::CommandPool, vk::raii::CommandPool> graphicsCommandPool = *transferCommandPool;
    if (gpu.queueFamilies.compute != gpu.queueFamilies.transfer) {
        computeCommandPool = decltype(computeCommandPool) {
            std::in_place_type<vk::raii::CommandPool>,
            gpu.device,
            vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute },
        };
    }
    else if (gpu.queueFamilies.graphicsPresent != gpu.queueFamilies.transfer) {
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

                mippedCubemapGenerator.recordCommands(cb, mippedCubemapGeneratorPipelines, eqmapImage);

                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                    {}, {}, {},
                    vk::ImageMemoryBarrier {
                        vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
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
            }, visit_as<vk::CommandPool>(computeCommandPool), gpu.queues.compute }),
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
