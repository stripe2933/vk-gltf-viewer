export module vk_gltf_viewer:imgui.TaskCollector;

import std;
export import glm;
import imgui.internal;
export import ImGuizmo;
export import vulkan_hpp;
export import :AppState;
export import :control.Task;
export import :helpers.full_optional;

namespace vk_gltf_viewer::control {
    export class ImGuiTaskCollector {
    public:
        ImGuiTaskCollector(std::vector<Task> &tasks, const ImVec2 &framebufferSize, const vk::Rect2D &oldPassthruRect);
        ~ImGuiTaskCollector();

        ImGuiTaskCollector menuBar(const std::list<std::filesystem::path> &recentGltfs, const std::list<std::filesystem::path> &recentSkyboxes) &&;
        ImGuiTaskCollector assetInspector(const std::optional<std::tuple<fastgltf::Asset&, const std::filesystem::path&, std::optional<std::size_t>&, std::span<const vk::DescriptorSet>>> &assetAndAssetDirAndAssetInspectorMaterialIndexAssetTextureImGuiDescriptorSets) &&;
        ImGuiTaskCollector sceneHierarchy(const std::optional<std::tuple<fastgltf::Asset&, std::size_t, const std::variant<std::vector<std::optional<bool>>, std::vector<bool>>&, const std::optional<std::uint16_t>&, const std::unordered_set<std::uint16_t>&>> &assetAndSceneIndexAndNodeVisibilitiesAndHoveringNodeIndexAndSelectedNodeIndices) &&;
        ImGuiTaskCollector nodeInspector(std::optional<std::pair<fastgltf::Asset &, const std::unordered_set<std::uint16_t>&>> assetAndSelectedNodeIndices) &&;
        ImGuiTaskCollector background(bool canSelectSkyboxBackground, full_optional<glm::vec3> &solidBackground) &&;
        ImGuiTaskCollector imageBasedLighting(const std::optional<std::pair<const AppState::ImageBasedLighting&, vk::DescriptorSet>> &imageBasedLightingInfoAndEqmapTextureImGuiDescriptorSet) &&;
        ImGuiTaskCollector inputControl(Camera &camera, bool& automaticNearFarPlaneAdjustment, bool &useFrustumCulling, full_optional<AppState::Outline> &hoveringNodeOutline, full_optional<AppState::Outline> &selectedNodeOutline) &&;
        ImGuiTaskCollector imguizmo(Camera &camera, const std::optional<std::tuple<fastgltf::Asset&, std::span<const glm::mat4>, std::uint16_t, ImGuizmo::OPERATION>> &assetAndNodeWorldTransformsAndSelectedNodeIndexAndImGuizmoOperation) &&;

    private:
        std::vector<Task> &tasks;
        ImRect centerNodeRect;
    };
}