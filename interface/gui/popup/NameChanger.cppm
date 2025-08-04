export module vk_gltf_viewer.gui.popup.NameChanger;

import std;

import vk_gltf_viewer.helpers.imgui;

namespace vk_gltf_viewer::gui::popup {
    export class NameChanger {
    public:
        static constexpr auto name = "Rename...";
        static constexpr bool modal = true;

        std::reference_wrapper<std::pmr::string> target;
        std::string hintText;

        NameChanger(std::pmr::string &target, std::string hintText)
            : target { target },
              hintText { std::move(hintText) } { }

        void show();

    private:
        std::string value { target.get() };
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

using namespace std::string_view_literals;

void vk_gltf_viewer::gui::popup::NameChanger::show() {
    ImGui::InputTextWithHint("Name", hintText, &value);

    ImGui::Separator();
    if (ImGui::Button("Okay")) {
        target.get() = std::move(value);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
}