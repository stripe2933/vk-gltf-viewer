module;

#include <compare>
#include <optional>

module vk_gltf_viewer;
import :AppState;

import glm;

vk_gltf_viewer::AppState::AppState() noexcept
    : camera {
        glm::gtc::lookAt(glm::vec3 { 1.f, 0.35f, 0.6f }, glm::vec3 { 0.f, 0.35f, 0.f }, glm::vec3 { 0.f, 1.f, 0.f }),
        glm::gtc::perspective(glm::radians(45.f), imGuiPassthruRect.GetWidth() / imGuiPassthruRect.GetHeight(), 1e-2f, 1e2f)
    } { }