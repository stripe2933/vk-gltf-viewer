module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.control.AppWindow;

import std;
import fmt;
export import glm;
export import vulkan;

export import vk_gltf_viewer.control.Task;

namespace vk_gltf_viewer::control {
    export class AppWindow {
    public:
        using task_queue_t = std::queue<Task>;

        explicit AppWindow(const vk::raii::Instance &instance LIFETIMEBOUND);
        ~AppWindow();

        [[nodiscard]] operator GLFWwindow*() const noexcept;

        [[nodiscard]] vk::SurfaceKHR getSurface() const noexcept;
        [[nodiscard]] glm::ivec2 getSize() const;
        [[nodiscard]] glm::ivec2 getFramebufferSize() const;
        [[nodiscard]] glm::dvec2 getCursorPos() const;
        [[nodiscard]] glm::vec2 getContentScale() const;

        void setTitle(const char *title) const;

        void pollEvents(task_queue_t &tasks) const;

    private:
        GLFWwindow *window;
        vk::raii::SurfaceKHR surface;

        [[nodiscard]] vk::raii::SurfaceKHR createSurface(const vk::raii::Instance &instance) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::control::AppWindow::AppWindow(const vk::raii::Instance &instance)
    : window { glfwCreateWindow(1280, 720, "Vulkan glTF Viewer", nullptr, nullptr) }
    , surface { createSurface(instance) } {
    glfwSetScrollCallback(window, [](GLFWwindow *window, double xoffset, double yoffset) {
        static_cast<task_queue_t*>(glfwGetWindowUserPointer(window))
            ->emplace(std::in_place_type<task::WindowScroll>, glm::dvec2 { xoffset, yoffset });
    });
    glfwSetTrackpadZoomCallback(window, [](GLFWwindow *window, double scale) {
        static_cast<task_queue_t*>(glfwGetWindowUserPointer(window))
            ->emplace(std::in_place_type<task::WindowTrackpadZoom>, scale);
    });
    glfwSetTrackpadRotateCallback(window, [](GLFWwindow *window, double angle) {
        static_cast<task_queue_t*>(glfwGetWindowUserPointer(window))
            ->emplace(std::in_place_type<task::WindowTrackpadRotate>, angle);
    });
    glfwSetDropCallback(window, [](GLFWwindow *window, int count, const char **paths) {
        std::vector<std::filesystem::path> fsPaths;
        fsPaths.reserve(count);
        for (int i = 0; i < count; ++i) {
            fsPaths.emplace_back(reinterpret_cast<const char8_t*>(paths[i]));
        }

        static_cast<task_queue_t*>(glfwGetWindowUserPointer(window))
            ->emplace(std::in_place_type<task::WindowDrop>, std::move(fsPaths));
    });
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *window, int width, int height) {
        static_cast<task_queue_t*>(glfwGetWindowUserPointer(window))
            ->emplace(std::in_place_type<task::WindowFramebufferSize>, glm::ivec2 { width, height });
    });
}

vk_gltf_viewer::control::AppWindow::~AppWindow() {
    glfwDestroyWindow(window);
}

vk_gltf_viewer::control::AppWindow::operator GLFWwindow*() const noexcept {
    return window;
}

vk::SurfaceKHR vk_gltf_viewer::control::AppWindow::getSurface() const noexcept {
    return *surface;
}

glm::ivec2 vk_gltf_viewer::control::AppWindow::getSize() const {
    glm::ivec2 size;
    glfwGetWindowSize(window, &size.x, &size.y);
    return size;
}

glm::ivec2 vk_gltf_viewer::control::AppWindow::getFramebufferSize() const {
    glm::ivec2 size;
    glfwGetFramebufferSize(window, &size.x, &size.y);
    return size;
}

glm::dvec2 vk_gltf_viewer::control::AppWindow::getCursorPos() const {
    glm::dvec2 pos;
    glfwGetCursorPos(window, &pos.x, &pos.y);
    return pos;
}

glm::vec2 vk_gltf_viewer::control::AppWindow::getContentScale() const {
    glm::vec2 scale;
    glfwGetWindowContentScale(window, &scale.x, &scale.y);
    return scale;
}

void vk_gltf_viewer::control::AppWindow::setTitle(const char *title) const {
    glfwSetWindowTitle(window, title);
}

void vk_gltf_viewer::control::AppWindow::pollEvents(task_queue_t &tasks) const {
    glfwSetWindowUserPointer(window, &tasks);
    glfwPollEvents();
}

vk::raii::SurfaceKHR vk_gltf_viewer::control::AppWindow::createSurface(const vk::raii::Instance &instance) const {
    if (VkSurfaceKHR surface; glfwCreateWindowSurface(*instance, window, nullptr, &surface) == VK_SUCCESS) {
        return { instance, surface };
    }

    const char *error;
    const int code = glfwGetError(&error);
    throw std::runtime_error { fmt::format("Failed to create window surface: {} (error code {})", error, code) };
}