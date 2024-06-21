module;

#include <optional>

#include <imgui_internal.h>

export module vk_gltf_viewer:AppState;

import :control.Camera;

namespace vk_gltf_viewer {
    export class AppState {
    public:
        // If passthru rect size is (0, 0), images which have the same extent of it cannot be allocated. Therefore, it is
        // initialized by (1, 1) extent, allocating the images with the size, and re-allocated with the actual size after
        // the second frame execution.
        ImRect imGuiPassthruRect { 0.f, 0.f, 1.f, 1.f };
        control::Camera camera;
        glm::uvec2 framebufferCursorPosition;
        std::optional<std::uint32_t> hoveringNodeIndex = std::nullopt,
                                     selectedNodeIndex = std::nullopt;

        AppState() noexcept;
    };
}