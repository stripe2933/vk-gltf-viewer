export module vk_gltf_viewer:control.Task;

import std;
export import vulkan_hpp;

namespace vk_gltf_viewer::control {
    namespace task {
        struct ChangePassthruRect { vk::Rect2D newRect; };
        struct LoadGltf { std::filesystem::path path; };
        struct CloseGltf { };
        struct LoadEqmap { std::filesystem::path path; };
        struct ChangeScene { std::size_t newSceneIndex; };
        struct ChangeNodeVisibilityType { };
        struct ChangeNodeVisibility { std::size_t nodeIndex; };
        struct SelectNodeFromSceneHierarchy { std::size_t nodeIndex; bool combine; };
        struct HoverNodeFromSceneHierarchy { std::size_t nodeIndex; };
        struct ChangeNodeLocalTransform { std::size_t nodeIndex; };
        struct TightenNearFarPlane { };
        struct ChangeCameraView { };
    }

    export using Task = std::variant<
        task::ChangePassthruRect,
        task::LoadGltf,
        task::CloseGltf,
        task::LoadEqmap,
        task::ChangeScene,
        task::ChangeNodeVisibilityType,
        task::ChangeNodeVisibility,
        task::SelectNodeFromSceneHierarchy,
        task::HoverNodeFromSceneHierarchy,
        task::ChangeNodeLocalTransform,
        task::TightenNearFarPlane,
        task::ChangeCameraView>;
}