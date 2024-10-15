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
export import :control.Task;
export import :helpers.full_optional;

namespace vk_gltf_viewer::control {
    export class ImGuiTaskCollector {
    public:
        ImGuiTaskCollector(std::vector<Task> &tasks, const ImVec2 &framebufferSize, const vk::Rect2D &oldPassthruRect);
        ~ImGuiTaskCollector();

        auto menuBar(const std::list<std::filesystem::path> &recentGltfs, const std::list<std::filesystem::path> &recentSkyboxes) && -> ImGuiTaskCollector;
        auto assetInspector(const std::optional<std::tuple<fastgltf::Asset&, const std::filesystem::path&, std::optional<std::size_t>&, std::span<const vk::DescriptorSet>>> &assetAndAssetDirAndAssetInspectorMaterialIndexAssetTextureImGuiDescriptorSets) && -> ImGuiTaskCollector;
        auto sceneHierarchy(const std::optional<std::tuple<const fastgltf::Asset&, std::size_t, const std::variant<std::vector<std::optional<bool>>, std::vector<bool>>&, const std::optional<std::size_t>&, const std::unordered_set<std::size_t>&>> &assetAndSceneIndexAndNodeVisibilitiesAndHoveringNodeIndexAndSelectedNodeIndices) && -> ImGuiTaskCollector;
        auto nodeInspector(std::optional<std::pair<fastgltf::Asset &, const std::unordered_set<std::size_t>&>> assetAndSelectedNodeIndices) && -> ImGuiTaskCollector;
        auto background(bool canSelectSkyboxBackground, full_optional<glm::vec3> &solidBackground) && -> ImGuiTaskCollector;
        auto imageBasedLighting(const std::optional<std::pair<const AppState::ImageBasedLighting&, vk::DescriptorSet>> &imageBasedLightingInfoAndEqmapTextureImGuiDescriptorSet) && -> ImGuiTaskCollector;
        auto inputControl(Camera &camera, bool& automaticNearFarPlaneAdjustment, full_optional<AppState::Outline> &hoveringNodeOutline, full_optional<AppState::Outline> &selectedNodeOutline) && -> ImGuiTaskCollector;
        auto imguizmo(Camera &camera, const std::optional<std::tuple<fastgltf::Asset&, std::span<const glm::mat4>, std::size_t, ImGuizmo::OPERATION>> &assetAndNodeWorldTransformsAndSelectedNodeIndexAndImGuizmoOperation) && -> ImGuiTaskCollector;

    private:
        std::vector<Task> &tasks;
        ImRect centerNodeRect;
    };
}