module;

#include <fastgltf/types.hpp>
#include <imgui.h>

export module vk_gltf_viewer:control.ImGui;

import std;
export import vulkan_hpp;
export import :AppState;

namespace vk_gltf_viewer::control::imgui {
    auto hdriEnvironments(ImTextureID eqmapTextureId, AppState &appState) -> void;
    auto assetInspector(fastgltf::Asset &asset, const std::filesystem::path &assetDir, AppState &appState) -> void;
    auto nodeInspector(fastgltf::Asset &asset, AppState &appState) -> void;
    auto inputControlSetting(AppState &appState) -> void;

    /**
     * Render ImGuizmo ViewManipulate based on the current AppState camera.
     * @param appState
     * @param passthruRectBR bottom-right position of the passthru rect.
     * @note <tt>ImGuizmo::BeginFrame()</tt> must be called before this function.
     */
    auto viewManipulate(AppState &appState, const ImVec2 &passthruRectBR) -> void;
}