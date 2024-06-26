module;

#include <optional>

#include <imgui_internal.h>

export module vk_gltf_viewer:AppState;

import vulkan_hpp;
import :control.Camera;

namespace vk_gltf_viewer {
    export class AppState {
    public:
        control::Camera camera;
        std::optional<std::uint32_t> hoveringNodeIndex = std::nullopt, selectedNodeIndex = std::nullopt;
        bool useBlurredSkybox = false;
        bool isUsingImGuizmo = false;

        AppState() noexcept;
    };
}