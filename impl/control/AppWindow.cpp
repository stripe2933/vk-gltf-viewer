module;

#include <algorithm>
#include <cmath>
#include <numbers>
#include <print>

#include <GLFW/glfw3.h>

module vk_gltf_viewer;
import :control.AppWindow;

import glm;

vk_gltf_viewer::control::AppWindow::AppWindow(
    const vk::raii::Instance &instance,
    GlobalState &globalState
) : GlfwWindow { 800, 480, "Vulkan glTF Viewer", instance },
    globalState { globalState } {
    onFramebufferSizeCallback(getFramebufferSize());
}

auto vk_gltf_viewer::control::AppWindow::update(
    float timeDelta
) -> void {
    // Move camera.
    if (cameraWasd[0] ^ cameraWasd[2] || cameraWasd[1] ^ cameraWasd[3]) {
        constexpr float CAMERA_SPEED = 1.f; // 1 units per second.
        constexpr float CAMERA_RUNNING_SPEED_MULTIPLIER = 2.f; // 2x speed when running.

        const auto [right, _, front, eye] = globalState.camera.getViewDecomposition();
        const glm::vec3 direction
            = front * static_cast<float>(cameraWasd[0] - cameraWasd[2])
            + right * static_cast<float>(cameraWasd[3] - cameraWasd[1]);

        glm::vec3 delta = CAMERA_SPEED * timeDelta * normalize(direction);
        if (cameraRunning) {
            delta *= CAMERA_RUNNING_SPEED_MULTIPLIER;
        }

        const glm::vec3 newEye = eye + delta;
        globalState.camera.view = glm::gtc::lookAt(newEye, newEye + front, glm::vec3 { 0.f, 1.f, 0.f });
    }
}

void vk_gltf_viewer::control::AppWindow::onFramebufferSizeCallback(
    glm::ivec2 size
) {
    globalState.camera.projection = glm::gtc::perspective(
        glm::radians(45.f), static_cast<float>(size.x) / size.y, 1e-2f, 1e2f);
}

auto vk_gltf_viewer::control::AppWindow::onScrollCallback(
    glm::dvec2 offset
    ) -> void {
    constexpr float MIN_FOV = glm::radians(15.f), MAX_FOV = glm::radians(120.f);
    constexpr float SCROLL_SENSITIVITY = 1e-2f;

    const auto near = globalState.camera.getNear(), far = globalState.camera.getFar();
    globalState.camera.projection = glm::gtc::perspective(
        std::clamp(globalState.camera.getFov() + SCROLL_SENSITIVITY * static_cast<float>(offset.y), MIN_FOV, MAX_FOV),
        globalState.camera.getAspectRatio(),
        near, far);
}

auto vk_gltf_viewer::control::AppWindow::onCursorPosCallback(
    glm::dvec2 position
) -> void {
    globalState.framebufferCursorPosition = glm::dvec2 { getContentScale() } * position;
}

auto vk_gltf_viewer::control::AppWindow::onKeyCallback(
    int key,
    int scancode,
    int action,
    int mods
) -> void {
    // Set WASD movement flags.
    if (action == GLFW_PRESS) {
        cameraWasd |= (key == GLFW_KEY_W) | (key == GLFW_KEY_A) << 1 | (key == GLFW_KEY_S) << 2 | (key == GLFW_KEY_D) << 3;
    } else if (action == GLFW_RELEASE) {
        cameraWasd &= (key != GLFW_KEY_W) | (key != GLFW_KEY_A) << 1 | (key != GLFW_KEY_S) << 2 | (key != GLFW_KEY_D) << 3;
    }

    // Running mode when pressing shift.
    if (key == GLFW_KEY_LEFT_SHIFT) {
        cameraRunning = action == GLFW_PRESS;
    }
}