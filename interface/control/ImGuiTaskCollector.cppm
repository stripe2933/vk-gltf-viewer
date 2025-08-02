module;

#include <nfd.hpp>

export module vk_gltf_viewer.imgui.TaskCollector;

import std;
import imgui.internal;

export import vk_gltf_viewer.AppState;
export import vk_gltf_viewer.control.Task;
export import vk_gltf_viewer.gltf.AssetExtended;
export import vk_gltf_viewer.Renderer;

namespace vk_gltf_viewer::control {
    export class ImGuiTaskCollector {
    public:
        ImGuiTaskCollector(std::queue<Task> &tasks, const ImRect &oldPassthruRect);
        ~ImGuiTaskCollector();

        void menuBar(const std::list<std::filesystem::path> &recentGltfs, const std::list<std::filesystem::path> &recentSkyboxes, nfdwindowhandle_t windowHandle);
        void animations(gltf::AssetExtended &assetExtended);
        void assetInspector(gltf::AssetExtended &assetExtended);
        void materialEditor(gltf::AssetExtended &assetExtended);
        void materialVariants(gltf::AssetExtended &assetExtended);
        void sceneHierarchy(gltf::AssetExtended &assetExtended);
        void nodeInspector(gltf::AssetExtended &assetExtended);
        void imageBasedLighting(const AppState::ImageBasedLighting &info, ImTextureRef eqmapTextureImGuiDescriptorSet);
        void rendererSetting(Renderer &renderer);
        void imguizmo(Renderer &renderer);
        void imguizmo(Renderer &renderer, gltf::AssetExtended &assetExtended);
        void dialog();

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