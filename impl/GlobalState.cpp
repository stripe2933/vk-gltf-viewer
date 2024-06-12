module vk_gltf_viewer;
import :GlobalState;

import glm;

auto vk_gltf_viewer::GlobalState::getInstance() noexcept -> GlobalState& {
    static GlobalState instance{};
    return instance;
}

vk_gltf_viewer::GlobalState::GlobalState() noexcept
    : camera {
        glm::gtc::lookAt(glm::vec3 { 5.f, 0.f, 0.f }, glm::vec3 { 0.f }, glm::vec3 { 0.f, 1.f, 0.f }),
        glm::gtc::perspective(glm::radians(45.f), 800.f / 480.f, 1e-2f, 1e2f)
    } { }