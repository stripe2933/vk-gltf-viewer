module;

#include <imgui_internal.h>

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
            };

            struct Cubemap {
                std::uint32_t size;
            };

            struct DiffuseIrradiance {
                std::array<glm::vec3, 9> sphericalHarmonicCoefficients;
            };

            struct Prefilteredmap {
                std::uint32_t size;
                std::uint32_t roughnessLevels;
                std::uint32_t sampleCount;
            };

            // TODO: set proper values.
            EquirectangularMap eqmap { std::getenv("EQMAP_PATH"), { 4096, 2048 } };
            Cubemap cubemap { 1024 };
            DiffuseIrradiance diffuseIrradiance {};
            Prefilteredmap prefilteredmap { 256, 9, 1024 };
        };

        control::Camera camera;
        std::optional<std::uint32_t> hoveringNodeIndex = std::nullopt, selectedNodeIndex = std::nullopt;
        bool useBlurredSkybox = false;
        bool isUsingImGuizmo = false;
        bool isPanning = false;
        full_optional<Outline> hoveringNodeOutline { std::in_place, 2.f, glm::vec4 { 1.f, 0.5f, 0.2f, 1.f } },
                               selectedNodeOutline { std::in_place, 2.f, glm::vec4 { 0.f, 1.f, 0.2f, 1.f } };
        std::optional<ImageBasedLighting> imageBasedLightingProperties = ImageBasedLighting{};

        AppState() noexcept;
    };
}