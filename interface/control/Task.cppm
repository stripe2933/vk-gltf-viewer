export module vk_gltf_viewer.control.Task;

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
        struct WindowDrop { std::vector<std::filesystem::path> paths; };
        struct WindowSize { glm::ivec2 size; };
        struct WindowContentScale { glm::vec2 scale; };

        struct ChangePassthruRect { ImRect newRect; };
        struct LoadGltf { std::filesystem::path path; };
        struct CloseGltf { };
        struct LoadEqmap { std::filesystem::path path; };
        struct ChangeScene { std::size_t newSceneIndex; };
        struct NodeVisibilityChanged { std::size_t nodeIndex; };
        struct NodeSelectionChanged { };
        struct HoverNodeFromGui { std::size_t nodeIndex; };
        struct NodeLocalTransformChanged { std::size_t nodeIndex; };

        /**
         * @brief Unlike <tt>NodeLocalTransformChanged</tt> struct, the transformation of the node indexed by
         * <tt>nodeIndex</tt> is not affected to its descendants' world transforms; only the immediate descendants local
         * transforms will be changed to match their original world transform.
         */
        struct NodeWorldTransformChanged { std::size_t nodeIndex; };

        struct MaterialPropertyChanged {
            enum Property {
                AlphaCutoff,
                AlphaMode,
                BaseColorFactor,
                BaseColorTextureTransform,
                DoubleSided,
                Emissive,
                EmissiveStrength,
                EmissiveTextureTransform,
                MetallicFactor,
                RoughnessFactor,
                MetallicRoughnessTextureTransform,
                NormalScale,
                NormalTextureTransform,
                OcclusionStrength,
                OcclusionTextureTransform,
                TextureTransformEnabled,
                Unlit,
                Ior,
            };

            std::size_t materialIndex;
            Property property;
        };
        struct SelectMaterialVariants { std::size_t variantIndex; };
        struct MorphTargetWeightChanged { std::size_t nodeIndex; std::size_t targetWeightStartIndex; std::size_t targetWeightCount; };
        struct BloomModeChanged{};
    }

    export using Task = std::variant<
        task::WindowKey,
        task::WindowCursorPos,
        task::WindowMouseButton,
        task::WindowScroll,
        task::WindowTrackpadZoom,
        task::WindowTrackpadRotate,
        task::WindowDrop,
        task::WindowSize,
        task::WindowContentScale,
        task::ChangePassthruRect,
        task::LoadGltf,
        task::CloseGltf,
        task::LoadEqmap,
        task::ChangeScene,
        task::NodeVisibilityChanged,
        task::NodeSelectionChanged,
        task::HoverNodeFromGui,
        task::NodeLocalTransformChanged,
        task::NodeWorldTransformChanged,
        task::MaterialPropertyChanged,
        task::SelectMaterialVariants,
        task::MorphTargetWeightChanged,
        task::BloomModeChanged>;
}