export module vk_gltf_viewer:control.Task;

import std;
export import vulkan_hpp;

namespace vk_gltf_viewer::control {
    namespace task {
        struct ChangePassthruRect { vk::Rect2D newRect; };
        struct ShowGltfLoadFileDialog { };
        struct LoadGltf { std::filesystem::path path; };
        struct CloseGltf { };
        struct ShowEqmapLoadFileDialog { };
        struct LoadEqmap { std::filesystem::path path; };
        struct ChangeScene { std::size_t newSceneIndex; };
        struct ChangeNodeVisibilityType { };
        struct ChangeNodeVisibility { std::uint16_t nodeIndex; };
        struct SelectNodeFromSceneHierarchy { std::uint16_t nodeIndex; bool combine; };
        struct HoverNodeFromSceneHierarchy { std::uint16_t nodeIndex; };
        struct ChangeNodeLocalTransform { std::uint16_t nodeIndex; };
        struct ChangeSelectedNodeWorldTransform{};
        struct TightenNearFarPlane { };
        struct ChangeCameraView { };
        struct InvalidateDrawCommandSeparation { };
    }

    export using Task = std::variant<
        task::ChangePassthruRect,
        task::ShowGltfLoadFileDialog,
        task::LoadGltf,
        task::CloseGltf,
        task::ShowEqmapLoadFileDialog,
        task::LoadEqmap,
        task::ChangeScene,
        task::ChangeNodeVisibilityType,
        task::ChangeNodeVisibility,
        task::SelectNodeFromSceneHierarchy,
        task::HoverNodeFromSceneHierarchy,
        task::ChangeNodeLocalTransform,
        task::ChangeSelectedNodeWorldTransform,
        task::TightenNearFarPlane,
        task::ChangeCameraView,
        task::InvalidateDrawCommandSeparation>;
}