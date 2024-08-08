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

    appState.camera.position *= std::powf(1.01f, -static_cast<float>(offset.y));
}

auto vk_gltf_viewer::control::AppWindow::onCursorPosCallback(
    glm::dvec2 position
) -> void {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) {
        appState.hoveringMousePosition.reset();
        return;
    }

    appState.hoveringMousePosition = position;
}

void vk_gltf_viewer::control::AppWindow::onMouseButtonCallback(
    int button,
    int action,
    int mods
) {
    if (const ImGuiIO &io = ImGui::GetIO(); io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            lastMouseDownPosition = getCursorPos();
        }
        else if (action == GLFW_RELEASE) {
            if (lastMouseDownPosition) {
                if (mods == GLFW_MOD_CONTROL) {
                    if (appState.hoveringNodeIndex) {
                        appState.selectedNodeIndices.emplace(*appState.hoveringNodeIndex);
                    }
                }
                else{
                    if (appState.hoveringNodeIndex) {
                        appState.selectedNodeIndices = { *appState.hoveringNodeIndex };
                    }
                    else {
                        appState.selectedNodeIndices.clear();
                    }
                }
                lastMouseDownPosition = std::nullopt;
            }
        }
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