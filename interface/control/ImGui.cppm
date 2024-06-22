module;

#include <fastgltf/types.hpp>
#include <imgui.h>

export module vk_gltf_viewer:control.ImGui;

export import :AppState;

namespace vk_gltf_viewer::control::imgui {
    auto assetSceneHierarchies(const fastgltf::Asset &asset, AppState &appState) -> void;
    auto nodeInspector(const fastgltf::Asset &asset, AppState &appState) -> void;

    /**
     * Render ImGuizmo ViewManipulate based on the current AppState camera.
     * @param appState
     * @param passthruRectBR bottom-left position of the passthru rect.
     * @return \p true if using ViewManipulate, \p false otherwise.
     * @note <tt>ImGuizmo::BeginFrame()</tt> must be called before this function.
     */
    auto viewManipulate(AppState &appState, const ImVec2 &passthruRectBR) -> bool;
}