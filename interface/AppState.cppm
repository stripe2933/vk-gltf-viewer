export module vk_gltf_viewer.AppState;

import std;
export import glm;

export import vk_gltf_viewer.helpers.full_optional;

namespace vk_gltf_viewer {
    export class AppState {
    public:
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

        AppState() noexcept;
        ~AppState();

        [[nodiscard]] std::list<std::filesystem::path>& getRecentGltfPaths() { return recentGltfPaths; }
        void pushRecentGltfPath(const std::filesystem::path &path);
        [[nodiscard]] std::list<std::filesystem::path>& getRecentSkyboxPaths() { return recentSkyboxPaths; }
        void pushRecentSkyboxPath(const std::filesystem::path &path);

    private:
        std::list<std::filesystem::path> recentGltfPaths;
        std::list<std::filesystem::path> recentSkyboxPaths;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::AppState::AppState() noexcept {
    if (const std::filesystem::path path { "recent_gltf.txt" }; std::filesystem::exists(path)) {
        std::ifstream file { path, std::ios::in };
        if (file.is_open()) {
            for (std::string line; std::getline(file, line);) {
                recentGltfPaths.emplace_back(line);
            }
        }
    }
    else {
        std::ofstream file { path, std::ios::out };
    }

    if (const std::filesystem::path path { "recent_skybox.txt" }; std::filesystem::exists(path)) {
        std::ifstream file { path, std::ios::in };
        if (file.is_open()) {
            for (std::string line; std::getline(file, line);) {
                recentSkyboxPaths.emplace_back(line);
            }
        }
    }
    else {
        std::ofstream file { path, std::ios::out };
    }
}

vk_gltf_viewer::AppState::~AppState() {
    if (std::ofstream file { "recent_gltf.txt", std::ios::out }; file.is_open()) {
        for (const std::filesystem::path &path : recentGltfPaths) {
            file << absolute(path).string() << '\n';
        }
    }

    if (std::ofstream file { "recent_skybox.txt", std::ios::out }; file.is_open()) {
        for (const std::filesystem::path &path : recentSkyboxPaths) {
            file << absolute(path).string() << '\n';
        }
    }
}

void vk_gltf_viewer::AppState::pushRecentGltfPath(const std::filesystem::path &path) {
    if (auto it = std::ranges::find(recentGltfPaths, path); it == recentGltfPaths.end()) {
        recentGltfPaths.emplace_front(path);
    }
    else {
        // The selected file is already in the list. Move it to the front.
        recentGltfPaths.splice(recentGltfPaths.begin(), recentGltfPaths, it);
    }
}

void vk_gltf_viewer::AppState::pushRecentSkyboxPath(const std::filesystem::path &path) {
    if (auto it = std::ranges::find(recentSkyboxPaths, path); it == recentSkyboxPaths.end()) {
        recentSkyboxPaths.emplace_front(path);
    }
    else {
        // The selected file is already in the list. Move it to the front.
        recentSkyboxPaths.splice(recentSkyboxPaths.begin(), recentSkyboxPaths, it);
    }
}