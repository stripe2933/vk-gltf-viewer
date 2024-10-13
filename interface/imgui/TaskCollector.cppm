module;

#include <fastgltf/types.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

export module vk_gltf_viewer:imgui.TaskCollector;

import std;
export import glm;
export import vulkan_hpp;
export import :AppState;
export import :control.Camera;
export import :helpers.full_optional;

namespace vk_gltf_viewer::imgui {
    namespace task {
        struct PassthruRectChanged { vk::Rect2D newRect; };
        struct LoadGltf { std::filesystem::path path; };
        struct CloseGltf { };
        struct LoadEqmap { std::filesystem::path path; };
        struct SceneChanged { std::size_t newSceneIndex; };
        struct NodeVisibilityTypeChanged { };
        struct NodeVisibilityChanged { std::size_t nodeIndex; };
        struct SelectedNodeChanged { std::size_t nodeIndex; bool combine; };
        struct HoveringNodeChanged { std::size_t nodeIndex; };
        struct NodeLocalTransformChanged { std::size_t nodeIndex; };
    }

    using Task = std::variant<
        task::PassthruRectChanged,
        task::LoadGltf,
        task::CloseGltf,
        task::LoadEqmap,
        task::SceneChanged,
        task::NodeVisibilityTypeChanged,
        task::NodeVisibilityChanged,
        task::SelectedNodeChanged,
        task::HoveringNodeChanged,
        task::NodeLocalTransformChanged>;

    class TaskCollector {
    public:
        TaskCollector(const ImVec2 &framebufferSize, const vk::Rect2D &oldPassthruRect);
        ~TaskCollector();

        auto menuBar(const std::list<std::filesystem::path> &recentGltfs, const std::list<std::filesystem::path> &recentSkyboxes) && -> TaskCollector;
        auto assetInspector(const std::optional<std::tuple<fastgltf::Asset&, const std::filesystem::path&, std::optional<std::size_t>&, std::span<const vk::DescriptorSet>>> &assetAndAssetDirAndAssetInspectorMaterialIndexAssetTextureImGuiDescriptorSets) && -> TaskCollector;
        auto sceneHierarchy(const std::optional<std::tuple<const fastgltf::Asset&, std::size_t, const std::variant<std::vector<std::optional<bool>>, std::vector<bool>>&, const std::optional<std::size_t>&, const std::unordered_set<std::size_t>&>> &assetAndSceneIndexAndNodeVisibilitiesAndHoveringNodeIndexAndSelectedNodeIndices) && -> TaskCollector;
        auto nodeInspector(std::optional<std::pair<fastgltf::Asset &, const std::unordered_set<std::size_t>&>> assetAndSelectedNodeIndices) && -> TaskCollector;
        auto background(bool canSelectSkyboxBackground, full_optional<glm::vec3> &solidBackground) && -> TaskCollector;
        auto imageBasedLighting(const std::optional<std::pair<const AppState::ImageBasedLighting&, vk::DescriptorSet>> &imageBasedLightingInfoAndEqmapTextureImGuiDescriptorSet) && -> TaskCollector;
        auto inputControl(control::Camera &camera, bool& automaticNearFarPlaneAdjustment, full_optional<AppState::Outline> &hoveringNodeOutline, full_optional<AppState::Outline> &selectedNodeOutline) && -> TaskCollector;
        auto imguizmo(control::Camera &camera, const std::optional<std::tuple<fastgltf::Asset&, std::span<const glm::mat4>, std::size_t, ImGuizmo::OPERATION>> &assetAndNodeWorldTransformsAndSelectedNodeIndexAndImGuizmoOperation) && -> TaskCollector;

        [[nodiscard]] auto collect() && -> std::vector<Task>;

    private:
        ImRect centerNodeRect;

        std::vector<Task> tasks;
    };
}