export module vk_gltf_viewer:AppState;

import std;
export import glm;
export import ImGuizmo;
export import :control.Camera;
export import :helpers.full_optional;

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
        bool automaticNearFarPlaneAdjustment = true;
        bool useFrustumCulling = false;
        full_optional<Outline> hoveringNodeOutline { std::in_place, 2.f, glm::vec4 { 1.f, 0.5f, 0.2f, 1.f } };
        full_optional<Outline> selectedNodeOutline { std::in_place, 2.f, glm::vec4 { 0.f, 1.f, 0.2f, 1.f } };
        bool canSelectSkyboxBackground = false; // TODO: bad design... this and background should be handled in a single field.
        full_optional<glm::vec4> background { std::in_place, 0.f, 0.f, 0.f, 1.f }; // nullopt -> use cubemap from the given equirectangular map image.
        std::optional<ImageBasedLighting> imageBasedLightingProperties;
        ImGuizmo::OPERATION imGuizmoOperation = ImGuizmo::OPERATION::TRANSLATE;

        AppState() noexcept;
        ~AppState();

        [[nodiscard]] const std::list<std::filesystem::path>& getRecentGltfPaths() const { return recentGltfPaths; }
        void pushRecentGltfPath(const std::filesystem::path &path);
        [[nodiscard]] const std::list<std::filesystem::path>& getRecentSkyboxPaths() const { return recentSkyboxPaths; }
        void pushRecentSkyboxPath(const std::filesystem::path &path);

    private:
        std::list<std::filesystem::path> recentGltfPaths;
        std::list<std::filesystem::path> recentSkyboxPaths;
    };
}