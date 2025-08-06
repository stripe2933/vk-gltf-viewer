module;

#include <cassert>

export module vk_gltf_viewer.gui.utils;

import std;
export import cstring_view;
export import fastgltf;
export import imgui.internal;

import vk_gltf_viewer.helpers.TempStringBuffer;

namespace vk_gltf_viewer::gui {
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Accessor> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Animation> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Buffer> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::BufferView> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Camera> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Image> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Light> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Material> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Mesh> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Node> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Sampler> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Scene> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Skin> v, std::size_t n) noexcept;
    export
    [[nodiscard]] cpp_util::cstring_view getDisplayName(std::span<const fastgltf::Texture> v, std::size_t n) noexcept;

    export void makeWindowVisible(ImGuiWindow *window);
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define DEFINE_GetDisplayName(Type) \
    cpp_util::cstring_view vk_gltf_viewer::gui::getDisplayName(std::span<const fastgltf::Type> v, std::size_t n) noexcept { \
        const auto &name = v[n].name; \
        if (!name.empty()) { \
            return name; \
        } \
        return tempStringBuffer.write("Unnamed " #Type " {}", n); \
    }

DEFINE_GetDisplayName(Accessor)
DEFINE_GetDisplayName(Animation)
DEFINE_GetDisplayName(Buffer)
DEFINE_GetDisplayName(BufferView)
DEFINE_GetDisplayName(Camera)
DEFINE_GetDisplayName(Image)
DEFINE_GetDisplayName(Light)
DEFINE_GetDisplayName(Material)
DEFINE_GetDisplayName(Mesh)
DEFINE_GetDisplayName(Node)
DEFINE_GetDisplayName(Sampler)
DEFINE_GetDisplayName(Scene)
DEFINE_GetDisplayName(Skin)
DEFINE_GetDisplayName(Texture)

void vk_gltf_viewer::gui::makeWindowVisible(ImGuiWindow* window) {
    if (window->DockNode && window->DockNode->TabBar) {
        // If window is docked and within the tab bar, make the tab bar's selected tab index to the current.
        // https://github.com/ocornut/imgui/issues/2887#issuecomment-849779358
        // TODO: if two docked window is in the same tab bar, it is not work.
        window->DockNode->TabBar->NextSelectedTabId = window->TabId;
    }
    else {
        // Otherwise, window is detached, therefore focusing it to make it top most.
        ImGui::FocusWindow(window);
    }
}