module;

#include <GLFW/glfw3.h>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:control.AppWindow;

import std;
import vku;
export import vulkan_hpp;
export import :AppState;
export import :control.Task;

namespace vk_gltf_viewer::control {
    export class AppWindow {
    public:
        AppState &appState;

        AppWindow(const vk::raii::Instance &instance LIFETIMEBOUND, AppState &appState);
        ~AppWindow();

        [[nodiscard]] operator GLFWwindow*() const noexcept;

        [[nodiscard]] vk::SurfaceKHR getSurface() const noexcept;

        [[nodiscard]] glm::ivec2 getSize() const;
        [[nodiscard]] glm::ivec2 getFramebufferSize() const;
        [[nodiscard]] glm::dvec2 getCursorPos() const;
        [[nodiscard]] glm::vec2 getContentScale() const;

        void setTitle(const char *title) {
            glfwSetWindowTitle(window, title);
        }

        void handleEvents(std::vector<Task> &tasks);

    private:
        GLFWwindow *window = glfwCreateWindow(1280, 720, "Vulkan glTF Viewer", nullptr, nullptr);
        vk::raii::SurfaceKHR surface;

        std::vector<Task> *pTasks;

        std::optional<glm::dvec2> lastMouseDownPosition = std::nullopt;

        [[nodiscard]] vk::raii::SurfaceKHR createSurface(const vk::raii::Instance &instance) const;

        void onScrollCallback(glm::dvec2 offset);
        void onTrackpadRotateCallback(double angle);
        void onCursorPosCallback(glm::dvec2 position);
        void onMouseButtonCallback(int button, int action, int mods);
        void onKeyCallback(int key, int scancode, int action, int mods);
        void onDropCallback(std::span<const char* const> paths);
    };
}