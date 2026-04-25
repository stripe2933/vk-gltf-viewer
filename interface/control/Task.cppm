export module vk_gltf_viewer.control.Task;

import std;
export import fastgltf;
export import glm;
export import imgui.internal;

namespace vk_gltf_viewer::control {
    export namespace task {
        struct WindowScroll { glm::dvec2 offset; };
        struct WindowTrackpadZoom { double scale; };
        struct WindowTrackpadRotate { double angle; };
        struct WindowDrop { std::vector<std::filesystem::path> paths; };
        struct WindowFramebufferSize { glm::ivec2 size; };

        struct ChangePassthruRect { ImRect newRect; };
        struct ChangeSampleCount { std::uint8_t sampleCount; };
        struct ChangeViewCount { std::size_t viewCount; };
        struct LoadGltf { std::filesystem::path path; };
        struct CloseGltf { };
        struct LoadEqmap { std::filesystem::path path; };
        struct ChangeScene { std::size_t newSceneIndex; };
        struct NodeVisibilityChanged { std::size_t nodeIndex; };
        struct NodeSelectionChanged { };
        struct NodeLocalTransformChanged { std::size_t nodeIndex; };

        /**
         * @brief Unlike <tt>NodeLocalTransformChanged</tt> struct, the transformation of the node indexed by
         * <tt>nodeIndex</tt> is not affected to its descendants' world transforms; only the immediate descendants local
         * transforms will be changed to match their original world transform.
         */
        struct NodeWorldTransformChanged { std::size_t nodeIndex; };
        struct MaterialAdded { };
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
        struct PrimitiveMaterialChanged { const fastgltf::Primitive *primitive; };
        struct MorphTargetWeightChanged { std::size_t nodeIndex; std::size_t targetWeightStartIndex; std::size_t targetWeightCount; };
        struct BloomModeChanged{};

        struct PickNodeAtPixel { std::uint32_t viewIndex; ImVec2 pixel; };
        struct PickNodesInSelectionRect { std::uint32_t viewIndex; ImRect selectionRect; };
    }

    export using Task = std::variant<
        task::WindowScroll,
        task::WindowTrackpadZoom,
        task::WindowTrackpadRotate,
        task::WindowDrop,
        task::WindowFramebufferSize,
        task::ChangePassthruRect,
        task::ChangeSampleCount,
        task::ChangeViewCount,
        task::LoadGltf,
        task::CloseGltf,
        task::LoadEqmap,
        task::ChangeScene,
        task::NodeVisibilityChanged,
        task::NodeSelectionChanged,
        task::NodeLocalTransformChanged,
        task::NodeWorldTransformChanged,
        task::MaterialAdded,
        task::MaterialPropertyChanged,
        task::PrimitiveMaterialChanged,
        task::MorphTargetWeightChanged,
        task::BloomModeChanged,
        task::PickNodeAtPixel,
        task::PickNodesInSelectionRect>;
}