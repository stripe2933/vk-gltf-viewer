export module vk_gltf_viewer.imgui.full_variant;

import std;
export import imgui;
export import vk_gltf_viewer.helpers.full_variant;

namespace ImGui {
    export template <typename... Ts>
    void Combo(const char *label, full_variant<Ts...> &variant, std::span<const char* const> labels) {
        const std::uint8_t index = variant.index();
        if (BeginCombo(label, labels[index])) {
            for (std::uint8_t i = 0; i < sizeof...(Ts); ++i) {
                const bool selected = index == i;
                if (Selectable(labels[i], selected)) {
                    variant.set_index(i);
                }

                if (selected) {
                    SetItemDefaultFocus();
                }
            }

            EndCombo();
        }
    }
}