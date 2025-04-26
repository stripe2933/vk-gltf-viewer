export module vk_gltf_viewer:control.Task;

import std;
export import glm;
export import imgui.internal;

namespace vk_gltf_viewer::control {
    export namespace task {
        struct WindowKey { int key; int scancode; int action; int mods; };
        struct WindowCursorPos { glm::dvec2 position; };
        struct WindowMouseButton { int button; int action; int mods; };
        struct WindowScroll { glm::dvec2 offset; };
        struct WindowTrackpadZoom { double scale; };
        struct WindowTrackpadRotate { double angle; };
        struct WindowDrop { std::span<const char* const> paths; };

        struct ChangePassthruRect { ImRect newRect; };
        struct LoadGltf { std::filesystem::path path; };
        struct CloseGltf { };
        struct LoadEqmap { std::filesystem::path path; };
        struct ChangeScene { std::size_t newSceneIndex; };
        struct ChangeNodeVisibilityType { };
        struct NodeVisibilityChanged { std::size_t nodeIndex; };
        struct SelectNode { std::uint16_t nodeIndex; bool combine; };
        struct HoverNodeFromSceneHierarchy { std::uint16_t nodeIndex; };
        struct NodeLocalTransformChanged { std::size_t nodeIndex; };
        struct SelectedNodeWorldTransformChanged{};
        struct TightenNearFarPlane { };
        struct CameraViewChanged { };
        struct MaterialPropertyChanged {
            enum Property {
                AlphaCutoff,
                AlphaMode,
                BaseColorFactor,
                BaseColorTextureTransform,
                BaseColorTextureTransformEnabled,
                DoubleSided,
                EmissiveFactor,
                EmissiveTextureTransform,
                EmissiveTextureTransformEnabled,
                MetallicFactor,
                RoughnessFactor,
                MetallicRoughnessTextureTransform,
                MetallicRoughnessTextureTransformEnabled,
                NormalScale,
                NormalTextureTransform,
                NormalTextureTransformEnabled,
                OcclusionStrength,
                OcclusionTextureTransform,
                OcclusionTextureTransformEnabled,
                Unlit,
            };

            std::size_t materialIndex;
            Property property;
        };
        struct SelectMaterialVariants { std::size_t variantIndex; };
        struct MorphTargetWeightChanged { std::size_t nodeIndex; std::size_t targetWeightStartIndex; std::size_t targetWeightCount; };
    }

    export using Task = std::variant<
        task::WindowKey,
        task::WindowCursorPos,
        task::WindowMouseButton,
        task::WindowScroll,
        task::WindowTrackpadZoom,
        task::WindowTrackpadRotate,
        task::WindowDrop,
        task::ChangePassthruRect,
        task::LoadGltf,
        task::CloseGltf,
        task::LoadEqmap,
        task::ChangeScene,
        task::ChangeNodeVisibilityType,
        task::NodeVisibilityChanged,
        task::SelectNode,
        task::HoverNodeFromSceneHierarchy,
        task::NodeLocalTransformChanged,
        task::SelectedNodeWorldTransformChanged,
        task::TightenNearFarPlane,
        task::CameraViewChanged,
        task::MaterialPropertyChanged,
        task::SelectMaterialVariants,
        task::MorphTargetWeightChanged>;
}