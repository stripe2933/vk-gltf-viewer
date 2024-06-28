module;

#include <optional>

#include <imgui_internal.h>

export module vk_gltf_viewer:AppState;

import vulkan_hpp;
import :control.Camera;
import :helpers.full_optional;

namespace vk_gltf_viewer {
    export class AppState {
    public:
        control::Camera camera;
        std::optional<std::uint32_t> hoveringNodeIndex = std::nullopt, selectedNodeIndex = std::nullopt;
        bool useBlurredSkybox = false;
        bool isUsingImGuizmo = false;
        bool isPanning = false;
        full_optional<std::pair<float, glm::vec4>> hoveringNodeOutline = std::pair { 2.f, glm::vec4 { 1.f, 0.5f, 0.2f, 1.f } },
                                                   selectedNodeOutline = std::pair { 2.f, glm::vec4 { 0.f, 1.f, 0.2f, 1.f } };

        AppState() noexcept;
    };
}