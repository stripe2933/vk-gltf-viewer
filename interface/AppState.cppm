module;

#include <optional>

#include <imgui_internal.h>

export module vk_gltf_viewer:AppState;

import :control.Camera;

namespace vk_gltf_viewer {
    export class AppState {
    public:
        control::Camera camera;
        glm::uvec2 framebufferCursorPosition;
        std::optional<std::uint32_t> hoveringNodeIndex, selectedNodeIndex;
        ImRect imGuiPassthruRect;

        [[nodiscard]] static auto getInstance() noexcept -> AppState&;

    private:
        AppState() noexcept;
    };
}