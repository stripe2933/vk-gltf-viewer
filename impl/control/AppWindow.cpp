module;

#include <algorithm>
#include <bitset>
#include <compare>

#include <GLFW/glfw3.h>
#include <imgui.h>

module vk_gltf_viewer;
import :control.AppWindow;

import glm;

vk_gltf_viewer::control::AppWindow::AppWindow(
    const vk::raii::Instance &instance,
    AppState &appState
) : GlfwWindow { 800, 480, "Vulkan glTF Viewer", instance },
    appState { appState } { }

auto vk_gltf_viewer::control::AppWindow::handleEvents(
    float timeDelta
) -> void {
    glfwPollEvents();

    // Move camera.
    if (cameraWasd[0] ^ cameraWasd[2] || cameraWasd[1] ^ cameraWasd[3]) {
        constexpr float CAMERA_SPEED = 1.f; // 1 units per second.

        const auto [right, _, front, eye] = appState.camera.getViewDecomposition();
        const glm::vec3 direction
            = front * static_cast<float>(cameraWasd[0] - cameraWasd[2])
            + right * static_cast<float>(cameraWasd[3] - cameraWasd[1]);

        glm::vec3 delta = CAMERA_SPEED * timeDelta * normalize(direction);
        if (cameraRunning) {
            constexpr float CAMERA_RUNNING_SPEED_MULTIPLIER = 2.f; // 2x speed when running.
            delta *= CAMERA_RUNNING_SPEED_MULTIPLIER;
        }

        const glm::vec3 newEye = eye + delta;
        appState.camera.view = glm::gtc::lookAt(newEye, newEye + front, glm::vec3 { 0.f, 1.f, 0.f });
    }
}

auto vk_gltf_viewer::control::AppWindow::onScrollCallback(
    glm::dvec2 offset
) -> void {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

    constexpr float MIN_FOV = glm::radians(15.f), MAX_FOV = glm::radians(120.f);
    constexpr float SCROLL_SENSITIVITY = 1e-2f;

    const auto near = appState.camera.getNear(), far = appState.camera.getFar();
    appState.camera.projection = glm::gtc::perspective(
        std::clamp(appState.camera.getFov() + SCROLL_SENSITIVITY * static_cast<float>(offset.y), MIN_FOV, MAX_FOV),
        appState.camera.getAspectRatio(),
        near, far);
}

auto vk_gltf_viewer::control::AppWindow::onCursorPosCallback(
    glm::dvec2 position
) -> void {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;
}

void vk_gltf_viewer::control::AppWindow::onMouseButtonCallback(
    int button,
    int action,
    int mods
) {
    if (appState.isUsingImGuizmo) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
            appState.isUsingImGuizmo = false;
        }
        // All mouse button events that are related to ImGuizmo should be ignored.
        return;
    }

    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        appState.selectedNodeIndex = appState.hoveringNodeIndex;
    }
}

auto vk_gltf_viewer::control::AppWindow::onKeyCallback(
    int key,
    int scancode,
    int action,
    int mods
) -> void {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureKeyboard) return;

    // Set WASD movement flags.
    if (action == GLFW_PRESS) {
        cameraWasd |= (key == GLFW_KEY_W) | (key == GLFW_KEY_A) << 1 | (key == GLFW_KEY_S) << 2 | (key == GLFW_KEY_D) << 3;
    } 
    else if (action == GLFW_RELEASE) {
        cameraWasd &= (key != GLFW_KEY_W) | (key != GLFW_KEY_A) << 1 | (key != GLFW_KEY_S) << 2 | (key != GLFW_KEY_D) << 3;
    }
    // Running mode when pressing shift.
    cameraRunning = cameraWasd.to_ulong() && (mods & GLFW_MOD_SHIFT);
}