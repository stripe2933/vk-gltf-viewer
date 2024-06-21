module;

#include <bitset>
#include <compare>

export module vk_gltf_viewer:control.AppWindow;

import vku;
export import :AppState;

namespace vk_gltf_viewer::control {
    export class AppWindow final : public vku::GlfwWindow {
    public:
        AppState &appState;

        AppWindow(const vk::raii::Instance &instance, AppState &appState);

        auto update(float timeDelta) -> void;

    private:
        std::bitset<4> cameraWasd = 0b0000;
        bool cameraRunning = false;

        auto onScrollCallback(glm::dvec2 offset) -> void override;
        auto onCursorPosCallback(glm::dvec2 position) -> void override;
        auto onMouseButtonCallback(int button, int action, int mods) -> void override;
        auto onKeyCallback(int key, int scancode, int action, int mods) -> void override;
    };
}