module;

#include <GLFW/glfw3.h>

export module vk_gltf_viewer:control.AppWindow;

import std;
import vku;
export import vulkan_hpp;
export import :AppState;

namespace vk_gltf_viewer::control {
    export class AppWindow {
    public:
        AppState &appState;

        AppWindow(const vk::raii::Instance &instance [[clang::lifetimebound]], AppState &appState);
        ~AppWindow();

        [[nodiscard]] operator GLFWwindow*() const noexcept;

        [[nodiscard]] auto getSurface() const noexcept -> vk::SurfaceKHR;

        [[nodiscard]] auto getSize() const -> glm::ivec2;
        [[nodiscard]] auto getFramebufferSize() const -> glm::ivec2;
        [[nodiscard]] auto getCursorPos() const -> glm::dvec2;
        [[nodiscard]] auto getContentScale() const -> glm::vec2;

        auto handleEvents(float timeDelta) -> void;

    private:
        GLFWwindow *window = glfwCreateWindow(800, 480, "Vulkan glTF Viewer", nullptr, nullptr);
        vk::raii::SurfaceKHR surface;

        std::optional<glm::dvec2> lastMouseDownPosition = std::nullopt;

        [[nodiscard]] auto createSurface(const vk::raii::Instance &instance) const -> vk::raii::SurfaceKHR;

        auto onScrollCallback(glm::dvec2 offset) -> void;
        auto onCursorPosCallback(glm::dvec2 position) -> void;
        auto onMouseButtonCallback(int button, int action, int mods) -> void;
        auto onKeyCallback(int key, int scancode, int action, int mods) -> void;
    };
}