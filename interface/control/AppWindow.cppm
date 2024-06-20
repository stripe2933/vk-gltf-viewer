module;

#include <bitset>
#include <compare>

export module vk_gltf_viewer:control.AppWindow;

import vku;
export import :GlobalState;

namespace vk_gltf_viewer::control {
    export class AppWindow final : public vku::GlfwWindow {
    public:
        GlobalState &globalState;

        AppWindow(const vk::raii::Instance &instance, GlobalState &globalState);

        auto update(float timeDelta) -> void;

    private:
        std::bitset<4> cameraWasd = 0b0000;
        bool cameraRunning = false;

        auto onFramebufferSizeCallback(glm::ivec2 size) -> void override;
        auto onScrollCallback(glm::dvec2 offset) -> void override;
        auto onCursorPosCallback(glm::dvec2 position) -> void override;
        auto onMouseButtonCallback(int button, int action, int mods) -> void override;
        auto onKeyCallback(int key, int scancode, int action, int mods) -> void override;
    };
}