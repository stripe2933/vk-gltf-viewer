module;

#include <nfd.hpp>

export module vk_gltf_viewer.imgui.TaskCollector;

import std;
export import glm;
import imgui.internal;
export import ImGuizmo;

export import vk_gltf_viewer.AppState;
export import vk_gltf_viewer.control.Task;
export import vk_gltf_viewer.gltf.Animation;
export import vk_gltf_viewer.gltf.StateCachedNodeVisibilityStructure;
export import vk_gltf_viewer.gltf.TextureUsages;
export import vk_gltf_viewer.helpers.full_optional;
export import vk_gltf_viewer.imgui.ColorSpaceAndUsageCorrectedTextures;

namespace vk_gltf_viewer::control {
    export class ImGuiTaskCollector {
    public:
        static std::optional<std::size_t> selectedMaterialIndex;

        ImGuiTaskCollector(std::queue<Task> &tasks, const ImRect &oldPassthruRect);
        ~ImGuiTaskCollector();

        void menuBar(const std::list<std::filesystem::path> &recentGltfs, const std::list<std::filesystem::path> &recentSkyboxes, nfdwindowhandle_t windowHandle);
        void animations(std::span<const gltf::Animation> animations, std::shared_ptr<std::vector<bool>> animationEnabled);
        void assetInspector(fastgltf::Asset &asset, const std::filesystem::path &assetDir);
        void assetTextures(fastgltf::Asset &asset, const imgui::ColorSpaceAndUsageCorrectedTextures &imGuiTextures, const gltf::TextureUsages &textureUsages);
        void materialEditor(fastgltf::Asset &asset, const imgui::ColorSpaceAndUsageCorrectedTextures &imGuiTextures);
        void materialVariants(const fastgltf::Asset &asset);
        void sceneHierarchy(fastgltf::Asset &asset, std::size_t sceneIndex, gltf::StateCachedNodeVisibilityStructure &nodeVisibilities, const std::optional<std::size_t> &hoveringNodeIndex, std::unordered_set<std::size_t> &selectedNodeIndices);
        void nodeInspector(fastgltf::Asset &asset, std::span<const gltf::Animation> animations, const std::vector<bool> &animationEnabled, std::unordered_set<std::size_t> &selectedNodeIndices);
        void background(bool canSelectSkyboxBackground, full_optional<glm::vec3> &solidBackground);
        void imageBasedLighting(const AppState::ImageBasedLighting &info, ImTextureID eqmapTextureImGuiDescriptorSet);
        void inputControl(bool& automaticNearFarPlaneAdjustment, full_optional<AppState::Outline> &hoveringNodeOutline, full_optional<AppState::Outline> &selectedNodeOutline, bool canSelectBloomModePerFragment);
        void imguizmo();
        void imguizmo(fastgltf::Asset &asset, const std::unordered_set<std::size_t> &selectedNodes, std::span<fastgltf::math::fmat4x4> nodeWorldTransforms, ImGuizmo::OPERATION operation, std::span<const gltf::Animation> animations, const std::vector<bool> &animationEnabled);

    private:
        std::queue<Task> &tasks;
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