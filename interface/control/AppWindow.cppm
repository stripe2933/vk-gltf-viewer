module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:control.AppWindow;

import std;
export import glm;
export import vulkan_hpp;
export import :control.Task;

namespace vk_gltf_viewer::control {
    export class AppWindow {
    public:
        explicit AppWindow(const vk::raii::Instance &instance LIFETIMEBOUND)
            : surface { createSurface(instance) } {
            glfwSetWindowUserPointer(window, this);

            glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode, int action, int mods) {
                static_cast<AppWindow*>(glfwGetWindowUserPointer(window))
                    ->pTasks->emplace(std::in_place_type<task::WindowKey>, key, scancode, action, mods);
            });
            glfwSetMouseButtonCallback(window, [](GLFWwindow *window, int button, int action, int mods) {
                static_cast<AppWindow*>(glfwGetWindowUserPointer(window))
                    ->pTasks->emplace(std::in_place_type<task::WindowMouseButton>, button, action, mods);
            });
            glfwSetScrollCallback(window, [](GLFWwindow *window, double xoffset, double yoffset) {
                static_cast<AppWindow*>(glfwGetWindowUserPointer(window))
                    ->pTasks->emplace(std::in_place_type<task::WindowScroll>, glm::dvec2 { xoffset, yoffset });
            });
            glfwSetTrackpadZoomCallback(window, [](GLFWwindow *window, double scale) {
                static_cast<AppWindow*>(glfwGetWindowUserPointer(window))
                    ->pTasks->emplace(std::in_place_type<task::WindowTrackpadZoom>, scale);
            });
            glfwSetTrackpadRotateCallback(window, [](GLFWwindow *window, double angle) {
                static_cast<AppWindow*>(glfwGetWindowUserPointer(window))
                    ->pTasks->emplace(std::in_place_type<task::WindowTrackpadRotate>, angle);
            });
            glfwSetDropCallback(window, [](GLFWwindow *window, int count, const char **paths) {
                static_cast<AppWindow*>(glfwGetWindowUserPointer(window))
                    ->pTasks->emplace(std::in_place_type<task::WindowDrop>, std::span { paths, static_cast<std::size_t>(count) });
            });
        }

        ~AppWindow() {
            glfwDestroyWindow(window);
        }

        [[nodiscard]] operator GLFWwindow*() const noexcept {
            return window;
        }

        [[nodiscard]] vk::SurfaceKHR getSurface() const noexcept {
            return *surface;
        }

        [[nodiscard]] glm::ivec2 getSize() const {
            glm::ivec2 size;
            glfwGetWindowSize(window, &size.x, &size.y);
            return size;
        }

        [[nodiscard]] glm::ivec2 getFramebufferSize() const {
            glm::ivec2 size;
            glfwGetFramebufferSize(window, &size.x, &size.y);
            return size;
        }

        [[nodiscard]] glm::dvec2 getCursorPos() const {
            glm::dvec2 pos;
            glfwGetCursorPos(window, &pos.x, &pos.y);
            return pos;
        }

        [[nodiscard]] glm::vec2 getContentScale() const {
            glm::vec2 scale;
            glfwGetWindowContentScale(window, &scale.x, &scale.y);
            return scale;
        }

        void setTitle(const char *title) const {
            glfwSetWindowTitle(window, title);
        }

        void pollEvents(std::queue<Task> &tasks) {
            pTasks = &tasks;
            glfwPollEvents();
        }

    private:
        GLFWwindow *window = glfwCreateWindow(1280, 720, "Vulkan glTF Viewer", nullptr, nullptr);
        vk::raii::SurfaceKHR surface;

        std::queue<Task> *pTasks;

        [[nodiscard]] vk::raii::SurfaceKHR createSurface(const vk::raii::Instance &instance) const {
            if (VkSurfaceKHR surface; glfwCreateWindowSurface(*instance, window, nullptr, &surface) == VK_SUCCESS) {
                return { instance, surface };
            }

            const char *error;
            const int code = glfwGetError(&error);
            throw std::runtime_error { std::format("Failed to create window surface: {} (error code {})", error, code) };
        }
    };
}