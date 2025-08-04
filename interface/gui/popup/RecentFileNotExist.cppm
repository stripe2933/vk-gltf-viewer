export module vk_gltf_viewer.gui.popup.RecentFileNotExist;

import std;
import imgui;

namespace vk_gltf_viewer::gui::popup {
    export struct RecentFileNotExist {
        static constexpr auto name = "File not exists";
        static constexpr bool modal = true; // TODO: should not be modal

        std::reference_wrapper<std::list<std::filesystem::path>> recentFiles;
        std::list<std::filesystem::path>::iterator target;

        void show();
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

using namespace std::string_view_literals;

void vk_gltf_viewer::gui::popup::RecentFileNotExist::show() {
    ImGui::TextUnformatted("The file you selected does not exist anymore.");
    ImGui::TextUnformatted("Do you want to remove it from the recent files list?");

    ImGui::Separator();
    if (ImGui::Button("Remove")) {
        recentFiles.get().erase(target);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
}