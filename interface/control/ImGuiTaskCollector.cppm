module;

#include <nfd.hpp>

export module vk_gltf_viewer:imgui.TaskCollector;

import std;
export import glm;
import imgui.internal;
export import ImGuizmo;
export import vulkan_hpp;
export import :AppState;
export import :control.Task;
export import :gltf.TextureUsage;
export import :helpers.full_optional;

namespace vk_gltf_viewer::control {
    export class ImGuiTaskCollector {
    public:
        static std::optional<std::size_t> selectedMaterialIndex;

        ImGuiTaskCollector(std::vector<Task> &tasks, const ImVec2 &framebufferSize, const vk::Rect2D &oldPassthruRect);
        ~ImGuiTaskCollector();

        void menuBar(const std::list<std::filesystem::path> &recentGltfs, const std::list<std::filesystem::path> &recentSkyboxes, nfdwindowhandle_t windowHandle);
        void animations(const fastgltf::Asset &asset, std::vector<bool> &animationEnabled);
        void assetInspector(fastgltf::Asset &asset, const std::filesystem::path &assetDir);
        void assetTextures(fastgltf::Asset &asset, std::span<const vk::DescriptorSet> assetTextureImGuiDescriptorSets, const gltf::TextureUsage &textureUsage);
        void materialEditor(fastgltf::Asset &asset, std::span<const vk::DescriptorSet> assetTextureImGuiDescriptorSets);
        void materialVariants(const fastgltf::Asset &asset);
        void sceneHierarchy(fastgltf::Asset &asset, std::size_t sceneIndex, const std::variant<std::vector<std::optional<bool>>, std::vector<bool>> &visibilities, const std::optional<std::uint16_t> &hoveringNodeIndex, const std::unordered_set<std::uint16_t> &selectedNodeIndices);
        void nodeInspector(fastgltf::Asset &asset, const std::unordered_set<std::uint16_t> &selectedNodeIndices);
        void background(bool canSelectSkyboxBackground, full_optional<glm::vec3> &solidBackground);
        void imageBasedLighting(const AppState::ImageBasedLighting &info, vk::DescriptorSet eqmapTextureImGuiDescriptorSet);
        void inputControl(Camera &camera, bool& automaticNearFarPlaneAdjustment, bool &useFrustumCulling, full_optional<AppState::Outline> &hoveringNodeOutline, full_optional<AppState::Outline> &selectedNodeOutline);
        void imguizmo(Camera &camera);
        void imguizmo(Camera &camera, fastgltf::math::fmat4x4 &selectedNodeWorldTransform, ImGuizmo::OPERATION operation);

    private:
        std::vector<Task> &tasks;
        ImRect centerNodeRect;

        bool assetInspectorCalled = false;
        bool materialEditorCalled = false;
        bool sceneHierarchyCalled = false;
        bool nodeInspectorCalled = false;
        bool imageBasedLightingCalled = false;

        void assetInfo(fastgltf::AssetInfo &assetInfo);
        void assetBuffers(std::span<fastgltf::Buffer> buffers, const std::filesystem::path &assetDir);
        void assetBufferViews(std::span<fastgltf::BufferView> bufferViews, std::span<fastgltf::Buffer> buffers);
        void assetImages(std::span<fastgltf::Image> images, const std::filesystem::path &assetDir);
        void assetSamplers(std::span<fastgltf::Sampler> samplers);
    };
}