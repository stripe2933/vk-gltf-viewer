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
        struct PopupNames {
            static constexpr cpp_util::cstring_view fileNotExists = "File Not Exists";
            static constexpr cpp_util::cstring_view resolveAnimationCollision = "Resolve Animation Collision";
            static constexpr cpp_util::cstring_view textureViewer = "Texture Viewer";
            static constexpr cpp_util::cstring_view renameMaterial = "Rename Material";
            static constexpr cpp_util::cstring_view renameScene = "Rename Scene";
        };

    public:
        /**
         * @brief Names of ImGui popups that are capturing the glTF asset by reference.
         *
         * Before the glTF asset is destroyed, you must close these popups, by calling <tt>gui::popup::close()</tt>.
         * @example
         * @code{.cpp}
         * for (auto name : ImGuiTaskCollector::assetPopupNames) {
         *     gui::popup::close(name);
         * }
         * @endcode
         */
        static constexpr auto assetPopupNames = {
            PopupNames::resolveAnimationCollision,
            PopupNames::textureViewer,
            PopupNames::renameMaterial,
            PopupNames::renameScene
        };

        ImGuiTaskCollector(std::queue<Task> &tasks, const ImRect &oldPassthruRect);
        ~ImGuiTaskCollector();

        void menuBar(std::list<std::filesystem::path> &recentGltfs, std::list<std::filesystem::path> &recentSkyboxes, nfdwindowhandle_t windowHandle);
        void animations(gltf::AssetExtended &assetExtended);
        void assetInspector(gltf::AssetExtended &assetExtended);
        void materialEditor(gltf::AssetExtended &assetExtended);
        void materialVariants(gltf::AssetExtended &assetExtended);
        void sceneHierarchy(gltf::AssetExtended &assetExtended);
        void nodeInspector(gltf::AssetExtended &assetExtended);
        void imageBasedLighting(const AppState::ImageBasedLighting &info, ImTextureRef eqmapTextureImGuiDescriptorSet);
        void rendererSetting(Renderer &renderer);
        void imguizmo(Renderer &renderer, std::size_t viewIndex);
        void imguizmo(Renderer &renderer, std::size_t viewIndex, gltf::AssetExtended &assetExtended);

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