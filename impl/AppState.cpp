module;

#include <compare>
#include <optional>

module vk_gltf_viewer;
import :AppState;

import glm;

auto vk_gltf_viewer::AppState::getInstance() noexcept -> AppState& {
    static AppState instance{};
    return instance;
}

vk_gltf_viewer::AppState::AppState() noexcept
    : camera {
        glm::gtc::lookAt(glm::vec3 { 1.f, 0.35f, 0.6f }, glm::vec3 { 0.f, 0.35f, 0.f }, glm::vec3 { 0.f, 1.f, 0.f }),
        glm::gtc::perspective(glm::radians(45.f), 800.f / 480.f, 1e-2f, 1e2f)
    },
    hoveringNodeIndex { std::nullopt },
    selectedNodeIndex { std::nullopt },
    // If passthru rect size is (0, 0), images which have the same extent of it cannot be allocated. Therefore, it is
    // initialized by (1, 1) extent, allocating the images with the size, and re-allocated with the actual size after
    // the second frame execution.
    imGuiPassthruRect { 0.f, 0.f, 1.f, 1.f }{ }