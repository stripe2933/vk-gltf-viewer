module;

#include <fastgltf/types.hpp>
#include <imgui.h>

export module vk_gltf_viewer:control.ImGui;

import std;
export import vulkan_hpp;
export import :AppState;

namespace vk_gltf_viewer::control::imgui {
    namespace task {
        struct LoadGltf {
            std::filesystem::path path;
        };

        struct CloseGltf { };

        struct LoadEqmap {
            std::filesystem::path path;
        };

        using type = std::variant<std::monostate, LoadGltf, CloseGltf, LoadEqmap>;
    };

    [[nodiscard]] auto menuBar() -> task::type;

    auto skybox(AppState &appState) -> void;
    auto hdriEnvironments(vk::DescriptorSet eqmapTexture, AppState &appState) -> void;

    // fastgltf Asset related.
    auto assetInfos(fastgltf::Asset &asset) -> void;
    auto assetBufferViews(fastgltf::Asset &asset) -> void;
    auto assetBuffers(fastgltf::Asset &asset, const std::filesystem::path &assetDir) -> void;
    auto assetImages(fastgltf::Asset &asset, const std::filesystem::path &assetDir) -> void;
    auto assetSamplers(fastgltf::Asset &asset) -> void;
    auto assetMaterials(fastgltf::Asset &asset, AppState &appState, std::span<const vk::DescriptorSet> assetTextures) -> void;
    auto assetSceneHierarchies(const fastgltf::Asset &asset, AppState &appState) -> void;

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