module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>

module vk_gltf_viewer;
import :control.AppWindow;

import std;
import glm;

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

auto vk_gltf_viewer::control::AppWindow::handleEvents(
    float timeDelta
) -> void {
    glfwPollEvents();
}

auto vk_gltf_viewer::control::AppWindow::createSurface(
    const vk::raii::Instance &instance
) const -> vk::raii::SurfaceKHR {
    if (VkSurfaceKHR surface; glfwCreateWindowSurface(*instance, window, nullptr, &surface) == VK_SUCCESS) {
        return { instance, surface };
    }

    const char *error;
    const int code = glfwGetError(&error);
    throw std::runtime_error { std::format("Failed to create window surface: {} (error code {})", error, code) };
}

auto vk_gltf_viewer::control::AppWindow::onScrollCallback(
    glm::dvec2 offset
) -> void {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

    const auto [_, _2, front, eye] = appState.camera.getViewDecomposition();
    constexpr float SENSITIVITY = 1e-2f;
    const glm::vec3 newEye = eye + SENSITIVITY * front * static_cast<float>(offset.y);
    appState.camera.view = glm::gtc::lookAt(newEye, newEye + front, glm::vec3 { 0.f, 1.f, 0.f });
}

auto vk_gltf_viewer::control::AppWindow::onCursorPosCallback(
    glm::dvec2 position
) -> void {
    static std::optional<glm::dvec2> lastMousePosition = std::nullopt;

    // Determine if dragging mouse, see linxx's answer.
    // https://stackoverflow.com/questions/37194845/using-glfw-to-capture-mouse-dragging-c
    if (glfwGetMouseButton(pWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && lastMousePosition) {
        const glm::vec2 delta = position - *lastMousePosition;

        // TODO: look's like more explanation for below required for the intention.
        // Get ratio of movement by window size, i.e. (delta) / (window size).
        const glm::vec2 movementRatio = delta / glm::vec2 { getSize() };

        // Get near plane size of the perspective frustrum, in world space.
        // Top left point in clip space: (-1, -1, 1, 1) (we uses reverse Z)
        //     -> inverseProjectionView * vec4(-1, -1, 1, 1) in world space.
        // Top right point in clip space: (1, -1, 1, 1)
        //     -> inverseProjectionView * vec4(1, -1, 1, 1) in world space.
        // Therefore, the distance between two vector (=near plane width) is:
        //     length(inverseProjectionView * vec4(1, -1, 1, 1) - inverseProjectionView * vec4(-1, -1, 1, 1))
        //   = length(inverseProjectionView * vec4(2, 0, 0, 0))
        //   = length(2 * inverseProjectionView[0])
        //   = 2 * length(inverseProjectionView[0]).
        // Similarly, the distance between bottom left and top left (=near plane height) would be:
        //     2 * length(inverseProjectionView[1]).
        const glm::mat4 inverseProjectionView = inverse(appState.camera.projection * appState.camera.view);
        const float nearPlaneWidth  = 2.f * length(inverseProjectionView[0]),
                    nearPlaneHeight = 2.f * length(inverseProjectionView[1]);

        const auto [right, up, front, eye] = appState.camera.getViewDecomposition();
        const glm::vec3 newEye = eye + (nearPlaneWidth * -movementRatio.x) * right + (nearPlaneHeight * movementRatio.y) * up;
        appState.camera.view = glm::gtc::lookAt(newEye, newEye + front, glm::vec3 { 0.f, 1.f, 0.f });

        appState.isPanning = true;
    }

    lastMousePosition = position;

    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse || appState.isUsingImGuizmo) return;
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

    if (appState.isPanning) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
            appState.isPanning = false;
        }
    }

    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            lastMouseDownPosition = getCursorPos();
        }
        else if (action == GLFW_RELEASE) {
            if (lastMouseDownPosition) {
                appState.selectedNodeIndex = appState.hoveringNodeIndex;
                lastMouseDownPosition = std::nullopt;
            }
        };
    }
}

auto vk_gltf_viewer::control::AppWindow::onKeyCallback(
    int key,
    int scancode,
    int action,
    int mods
) -> void {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureKeyboard) return;
}