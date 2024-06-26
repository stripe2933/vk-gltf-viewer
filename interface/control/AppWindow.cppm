module;

#include <compare>
#include <optional>

export module vk_gltf_viewer:control.AppWindow;

import vku;
export import :AppState;

namespace vk_gltf_viewer::control {
    export class AppWindow final : public vku::GlfwWindow {
    public:
        AppState &appState;

        AppWindow(const vk::raii::Instance &instance, AppState &appState);

        auto handleEvents(float timeDelta) -> void;

    private:
        std::optional<glm::dvec2> lastMouseDownPosition = std::nullopt;

        auto onScrollCallback(glm::dvec2 offset) -> void override;
        auto onCursorPosCallback(glm::dvec2 position) -> void override;
        auto onMouseButtonCallback(int button, int action, int mods) -> void override;
        auto onKeyCallback(int key, int scancode, int action, int mods) -> void override;
    };
}