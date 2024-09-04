module;

#include <fastgltf/types.hpp>
#include <imgui.h>

export module vk_gltf_viewer:control.ImGui;

import std;
export import vulkan_hpp;
export import :AppState;

namespace vk_gltf_viewer::control::imgui {
    namespace task {
        struct LoadGltf { std::filesystem::path path; };
        struct CloseGltf { };
        struct LoadEqmap { std::filesystem::path path; };

        using type = std::variant<std::monostate, LoadGltf, CloseGltf, LoadEqmap>;
    };

    [[nodiscard]] auto menuBar(AppState &appState) -> task::type;

    auto skybox(AppState &appState) -> void;
    auto hdriEnvironments(vk::DescriptorSet eqmapTexture, AppState &appState) -> void;

    // fastgltf Asset related.
    auto assetInfos(AppState &appState) -> void;
    auto assetBufferViews(AppState &appState) -> void;
    auto assetBuffers(AppState &appState) -> void;
    auto assetImages(AppState &appState) -> void;
    auto assetSamplers(AppState &appState) -> void;
    auto assetMaterials(AppState &appState, std::span<const vk::DescriptorSet> assetTextures) -> void;
    auto assetSceneHierarchies(AppState &appState) -> void;

    auto nodeInspector(AppState &appState) -> void;
    auto inputControlSetting(AppState &appState) -> void;

    /**
     * Render ImGuizmo Manipulator with \p nodeTransform.
     * @param appState <tt>AppState</tt> struct instance that is necessary for view/projection calculation.
     * @param nodeTransform Transform matrix to manipulate.
     * @return Delta matrix that is calculated by inverse(oldTransform) * newTransform (which should be multiplied to
     * previous transform matrix) if manipulator adjusted, <tt>std::nullopt</tt> otherwise.
     * @pre <tt>ImGuizmo::BeginFrame()</tt> must be called before this function.
     */
    auto manipulate(const AppState &appState, const glm::mat4 &nodeTransform) -> std::optional<glm::mat4>;
    /**
     * Render ImGuizmo ViewManipulate based on the current AppState camera.
     * @param appState
     * @param passthruRectBR bottom-right position of the passthru rect.
     * @pre <tt>ImGuizmo::BeginFrame()</tt> must be called before this function.
     * @pre <tt>ImGuizmo::Manipulate()</tt> must be called before this function, otherwise
     */
    auto viewManipulate(AppState &appState, const ImVec2 &passthruRectBR) -> void;
}