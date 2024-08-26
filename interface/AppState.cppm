export module vk_gltf_viewer:AppState;

import std;
export import glm;
import :control.Camera;
import :helpers.full_optional;

namespace vk_gltf_viewer {
    export class AppState {
    public:
        struct Outline {
            float thickness;
            glm::vec4 color;
        };

        struct ImageBasedLighting {
            struct EquirectangularMap {
                std::filesystem::path path;
                glm::u32vec2 dimension;
            } eqmap;

            struct Cubemap {
                std::uint32_t size;
            } cubemap;

            struct DiffuseIrradiance {
                std::array<glm::vec3, 9> sphericalHarmonicCoefficients;
            } diffuseIrradiance;

            struct Prefilteredmap {
                std::uint32_t size;
                std::uint32_t roughnessLevels;
                std::uint32_t sampleCount;
            } prefilteredmap;
        };

        control::Camera camera;
        std::optional<glm::vec2> hoveringMousePosition;
        std::optional<std::uint32_t> hoveringNodeIndex = std::nullopt;
        std::unordered_set<std::size_t> selectedNodeIndices;
        std::unordered_set<std::size_t> renderingNodeIndices;
        full_optional<Outline> hoveringNodeOutline { std::in_place, 2.f, glm::vec4 { 1.f, 0.5f, 0.2f, 1.f } };
        full_optional<Outline> selectedNodeOutline { std::in_place, 2.f, glm::vec4 { 0.f, 1.f, 0.2f, 1.f } };
        bool canSelectSkyboxBackground = false; // TODO: bad design... this and background should be handled in a single field.
        full_optional<glm::vec3> background { std::in_place, 0.f, 0.f, 0.f }; // nullopt -> use cubemap from the given equirectangular map image.
        std::optional<ImageBasedLighting> imageBasedLightingProperties;
        bool useTristateVisibility = true;

        std::optional<std::size_t> imGuiAssetInspectorMaterialIndex;
        std::optional<std::size_t> imGuiAssetInspectorSceneIndex;

        AppState() noexcept;
    };
}