module;

#ifdef _MSC_VER
// TODO: I don't know why this workaround work... I've never seen this C++20 module issue in MSVC. Need investigation.
#include <fstream>
#endif

module vk_gltf_viewer;
import :AppState;

import std;
import vku;

vk_gltf_viewer::AppState::AppState() noexcept
    : camera {
        glm::vec3 { 0.f, 0.f, 5.f }, normalize(glm::vec3 { 0.f, 0.f, -1.f }), glm::vec3 { 0.f, 1.f, 0.f },
        glm::radians(45.f), 1.f /* will be determined by passthru rect dimension */, 1e-2f, 10.f,
        5.f,
    } {
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

auto vk_gltf_viewer::AppState::pushRecentGltfPath(const std::filesystem::path &path) -> void {
    if (auto it = std::ranges::find(recentGltfPaths, path); it == recentGltfPaths.end()) {
        recentGltfPaths.emplace_front(path);
    }
    else {
        // The selected file is already in the list. Move it to the front.
        recentGltfPaths.splice(recentGltfPaths.begin(), recentGltfPaths, it);
    }
}

auto vk_gltf_viewer::AppState::pushRecentSkyboxPath(const std::filesystem::path &path) -> void {
    if (auto it = std::ranges::find(recentSkyboxPaths, path); it == recentSkyboxPaths.end()) {
        recentSkyboxPaths.emplace_front(path);
    }
    else {
        // The selected file is already in the list. Move it to the front.
        recentSkyboxPaths.splice(recentSkyboxPaths.begin(), recentSkyboxPaths, it);
    }
}
