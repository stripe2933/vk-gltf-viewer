module;

#include <nfd.hpp>

export module vk_gltf_viewer.imgui.TaskCollector;

import std;
export import glm;
import imgui.internal;
export import ImGuizmo;

export import vk_gltf_viewer.AppState;
export import vk_gltf_viewer.control.Task;
export import vk_gltf_viewer.gltf.AssetExtended;
export import vk_gltf_viewer.helpers.full_optional;

namespace vk_gltf_viewer::control {
    export class ImGuiTaskCollector {
    public:
        ImGuiTaskCollector(std::queue<Task> &tasks, const ImRect &oldPassthruRect);
        ~ImGuiTaskCollector();

        void menuBar(const std::list<std::filesystem::path> &recentGltfs, const std::list<std::filesystem::path> &recentSkyboxes, nfdwindowhandle_t windowHandle);
        void animations(const std::shared_ptr<gltf::AssetExtended> &assetExtended);
        void assetInspector(gltf::AssetExtended &assetExtended);
        void materialEditor(gltf::AssetExtended &assetExtended);
        void materialVariants(gltf::AssetExtended &assetExtended);
        void sceneHierarchy(gltf::AssetExtended &assetExtended);
        void nodeInspector(gltf::AssetExtended &assetExtended);
        void background(bool canSelectSkyboxBackground, full_optional<glm::vec3> &solidBackground);
        void imageBasedLighting(const AppState::ImageBasedLighting &info, ImTextureID eqmapTextureImGuiDescriptorSet);
        void inputControl(bool& automaticNearFarPlaneAdjustment, full_optional<AppState::Outline> &hoveringNodeOutline, full_optional<AppState::Outline> &selectedNodeOutline, bool canSelectBloomModePerFragment);
        void imguizmo();
        void imguizmo(gltf::AssetExtended &assetExtended, ImGuizmo::OPERATION operation);

    private:
        std::queue<Task> &tasks;
        ImRect centerNodeRect;

        bool assetInspectorCalled = false;
        bool materialEditorCalled = false;
        bool sceneHierarchyCalled = false;
        bool nodeInspectorCalled = false;
        bool imageBasedLightingCalled = false;
    };
}