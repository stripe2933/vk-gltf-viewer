module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

module vk_gltf_viewer;
import :control.AppWindow;

import std;
import glm;
import ImGuizmo;
import :global;
import :helpers.ranges;

vk_gltf_viewer::control::AppWindow::AppWindow(
    const vk::raii::Instance &instance,
    AppState &appState
) : surface { createSurface(instance) },
    appState { appState } {
    glfwSetWindowUserPointer(window, this);

    glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode, int action, int mods) {
        static_cast<AppWindow*>(glfwGetWindowUserPointer(window))->onKeyCallback(key, scancode, action, mods);
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow *window, double xpos, double ypos) {
        static_cast<AppWindow*>(glfwGetWindowUserPointer(window))->onCursorPosCallback({ xpos, ypos });
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow *window, int button, int action, int mods) {
        static_cast<AppWindow*>(glfwGetWindowUserPointer(window))->onMouseButtonCallback(button, action, mods);
    });
    glfwSetScrollCallback(window, [](GLFWwindow *window, double xoffset, double yoffset) {
        static_cast<AppWindow*>(glfwGetWindowUserPointer(window))->onScrollCallback({ xoffset, yoffset });
    });
    glfwSetTrackpadZoomCallback(window, [](GLFWwindow *window, double scale) {
        // TODO: should onScrollCallback method be renamed to zoomScene?
        static_cast<AppWindow*>(glfwGetWindowUserPointer(window))->onScrollCallback({ 0.0, 1e2 * scale });
    });
    glfwSetTrackpadRotateCallback(window, [](GLFWwindow *window, double angle) {
        // TODO: should onScrollCallback method be renamed to zoomScene?
        static_cast<AppWindow*>(glfwGetWindowUserPointer(window))->onTrackpadRotateCallback(angle);
    });
    glfwSetDropCallback(window, [](GLFWwindow *window, int count, const char *paths[]) {
        static_cast<AppWindow*>(glfwGetWindowUserPointer(window))->onDropCallback({ paths, static_cast<std::size_t>(count) });
    });
}

vk_gltf_viewer::control::AppWindow::~AppWindow() {
    glfwDestroyWindow(window);
}

vk_gltf_viewer::control::AppWindow::operator GLFWwindow*() const noexcept {
    return window;
}

auto vk_gltf_viewer::control::AppWindow::getSurface() const noexcept -> vk::SurfaceKHR {
    return *surface;
}

auto vk_gltf_viewer::control::AppWindow::getSize() const -> glm::ivec2 {
    glm::ivec2 size;
    glfwGetWindowSize(window, &size.x, &size.y);
    return size;
}

auto vk_gltf_viewer::control::AppWindow::getFramebufferSize() const -> glm::ivec2 {
    glm::ivec2 size;
    glfwGetFramebufferSize(window, &size.x, &size.y);
    return size;
}

auto vk_gltf_viewer::control::AppWindow::getCursorPos() const -> glm::dvec2 {
    glm::dvec2 pos;
    glfwGetCursorPos(window, &pos.x, &pos.y);
    return pos;
}

auto vk_gltf_viewer::control::AppWindow::getContentScale() const -> glm::vec2 {
    glm::vec2 scale;
    glfwGetWindowContentScale(window, &scale.x, &scale.y);
    return scale;
}

auto vk_gltf_viewer::control::AppWindow::handleEvents(std::vector<Task> &tasks) -> void {
    pTasks = &tasks;
    glfwPollEvents();
}

auto vk_gltf_viewer::control::AppWindow::createSurface(const vk::raii::Instance &instance) const -> vk::raii::SurfaceKHR {
    if (VkSurfaceKHR surface; glfwCreateWindowSurface(*instance, window, nullptr, &surface) == VK_SUCCESS) {
        return { instance, surface };
    }

    const char *error;
    const int code = glfwGetError(&error);
    throw std::runtime_error { std::format("Failed to create window surface: {} (error code {})", error, code) };
}

void vk_gltf_viewer::control::AppWindow::onScrollCallback(glm::dvec2 offset) {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

    const float factor = std::powf(1.01f, -offset.y);
    const glm::vec3 displacementToTarget = appState.camera.direction * appState.camera.targetDistance;
    appState.camera.targetDistance *= factor;
    appState.camera.position += (1.f - factor) * displacementToTarget;

    pTasks->emplace_back(std::in_place_type<task::ChangeCameraView>);
}

void vk_gltf_viewer::control::AppWindow::onTrackpadRotateCallback(double angle) {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

    // Rotate the camera around the Y-axis lied on the target point.
    const glm::vec3 target = appState.camera.position + appState.camera.direction * appState.camera.targetDistance;
    const glm::mat4 rotation = rotate(-glm::radians<float>(angle), glm::vec3 { 0.f, 1.f, 0.f });
    appState.camera.direction = glm::mat3 { rotation } * appState.camera.direction;
    appState.camera.position = target - appState.camera.direction * appState.camera.targetDistance;

    pTasks->emplace_back(std::in_place_type<task::ChangeCameraView>);
}

void vk_gltf_viewer::control::AppWindow::onCursorPosCallback(glm::dvec2 position) {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) {
        appState.hoveringMousePosition.reset();
        return;
    }

    appState.hoveringMousePosition = position;
}

void vk_gltf_viewer::control::AppWindow::onMouseButtonCallback(int button, int action, int mods) {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            lastMouseDownPosition = getCursorPos();
        }
        else if (action == GLFW_RELEASE) {
            if (lastMouseDownPosition) {
                if (appState.gltfAsset) {
                    if (appState.gltfAsset->hoveringNodeIndex) {
                        pTasks->emplace_back(std::in_place_type<task::SelectNode>, *appState.gltfAsset->hoveringNodeIndex, mods == GLFW_MOD_CONTROL);
                        global::shouldNodeInSceneHierarchyScrolledToBeVisible = true;
                    }
                    else {
                        appState.gltfAsset->selectedNodeIndices.clear();
                    }
                }
                lastMouseDownPosition = std::nullopt;
            }
        }
    }
}

void vk_gltf_viewer::control::AppWindow::onKeyCallback(int key, int scancode, int action, int mods) {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureKeyboard) return;

    if (action == GLFW_PRESS && appState.canManipulateImGuizmo()) {
        switch (key) {
            case GLFW_KEY_T:
                appState.imGuizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
                break;
            case GLFW_KEY_R:
                appState.imGuizmoOperation = ImGuizmo::OPERATION::ROTATE;
                break;
            case GLFW_KEY_S:
                appState.imGuizmoOperation = ImGuizmo::OPERATION::SCALE;
                break;
        }
    }
}

void vk_gltf_viewer::control::AppWindow::onDropCallback(std::span<const char * const> paths) {
    if (paths.empty()) return;

    static constexpr auto supportedSkyboxExtensions = {
        ".hdr",
#ifdef SUPPORT_EXR_SKYBOX
        ".exr",
#endif
    };

    const std::filesystem::path path = paths[0];
    if (std::filesystem::is_directory(path)) {
        // If directory contains glTF file, load it.
        for (const std::filesystem::path &childPath : std::filesystem::directory_iterator { path }) {
            if (ranges::one_of(childPath.extension(), ".gltf", ".glb")) {
                pTasks->emplace_back(std::in_place_type<task::LoadGltf>, childPath);
                return;
            }
        }
    }
    else if (const std::filesystem::path extension = path.extension(); ranges::one_of(extension, ".gltf", ".glb")) {
        pTasks->emplace_back(std::in_place_type<task::LoadGltf>, path);
    }
    else if (std::ranges::contains(supportedSkyboxExtensions, extension)) {
        pTasks->emplace_back(std::in_place_type<task::LoadEqmap>, path);
    }
}
