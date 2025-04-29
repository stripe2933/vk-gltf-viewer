module vk_gltf_viewer;
import :AppState;

import std;
import :gltf.algorithm.traversal;
import :helpers.functional;
import :helpers.ranges;

void vk_gltf_viewer::AppState::GltfAsset::setScene(std::size_t sceneIndex) noexcept {
    this->sceneIndex = sceneIndex;
    visit([this](auto &visibilities) {
        std::ranges::fill(visibilities, false);
        gltf::algorithm::traverseScene(asset, getScene(), [&](std::size_t nodeIndex) {
           visibilities[nodeIndex] = true;
        });
    }, nodeVisibilities);
    selectedNodeIndices.clear();
    hoveringNodeIndex.reset();
}

std::unordered_set<std::size_t> vk_gltf_viewer::AppState::GltfAsset::getVisibleNodeIndices() const noexcept {
    return visit(multilambda {
        [this](std::span<const std::optional<bool>> tristateVisibilities) {
            return tristateVisibilities
                | ranges::views::enumerate
                | std::views::filter(decomposer([this](auto nodeIndex, const std::optional<bool> &visibility) {
                    return visibility.value_or(true) && asset.nodes[nodeIndex].meshIndex.has_value();
                }))
                | std::views::keys
                | std::views::transform([](auto nodeIndex) { return static_cast<std::size_t>(nodeIndex); })
                | std::ranges::to<std::unordered_set>();
        },
        [this](const std::vector<bool> &visibilities) {
            return visibilities
                | ranges::views::enumerate
                | std::views::filter(decomposer([this](auto nodeIndex, bool visibility) {
                    return visibility && asset.nodes[nodeIndex].meshIndex.has_value();
                }))
                | std::views::keys
                | std::views::transform([](auto nodeIndex) { return static_cast<std::size_t>(nodeIndex); })
                | std::ranges::to<std::unordered_set>();
        }
    }, nodeVisibilities);
}


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

void vk_gltf_viewer::AppState::GltfAsset::switchNodeVisibilityType() {
    visit(multilambda {
        [this](std::span<const std::optional<bool>> visibilities) {
            // Note: std::vector<bool> must be constructed and move-assigned to nodeVisibilities.
            // If it is generated in-place, nodeVisibilities will be created while reading visibilities span, which
            // already has corrupted value by overwritten data.
            nodeVisibilities = std::vector<bool> {
                std::from_range,
                visibilities | std::views::transform([](const std::optional<bool> &visibility) {
                    return visibility.value_or(true);
                })
            };
        },
        [this](const std::vector<bool> &visibilities) {
            nodeVisibilities.emplace<std::vector<std::optional<bool>>>(visibilities.size(), true);
        },
    }, nodeVisibilities);
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
