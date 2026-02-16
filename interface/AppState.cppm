export module vk_gltf_viewer.AppState;

import std;
export import glm;

namespace vk_gltf_viewer {
    export struct AppState {
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

        std::optional<ImageBasedLighting> imageBasedLightingProperties;
    };
}