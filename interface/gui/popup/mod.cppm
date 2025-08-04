export module vk_gltf_viewer.gui.popup;
export import vk_gltf_viewer.gui.popup.AnimationCollisionResolver;
export import vk_gltf_viewer.gui.popup.NameChanger;
export import vk_gltf_viewer.gui.popup.RecentFileNotExist;
export import vk_gltf_viewer.gui.popup.TextureViewer;

import std;
import imgui;

namespace vk_gltf_viewer::gui::popup {
    export using PopupVariant = std::variant<TextureViewer, AnimationCollisionResolver, NameChanger, RecentFileNotExist>;

    export std::list<PopupVariant> waitList;
    std::list<PopupVariant> opened;

    /**
     * @brief Check if any dialog (modal popup) is opened.
     * @return <tt>true</tt> if any dialog is opened, <tt>false</tt> otherwise.
     */
    export
    [[nodiscard]] bool isDialogOpened() noexcept;

    export void process();
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

bool vk_gltf_viewer::gui::popup::isDialogOpened() noexcept {
    return std::ranges::any_of(opened, [](const auto &popup) noexcept {
        return std::visit([]<typename Popup>(const Popup&) {
            return Popup::modal;
        }, popup);
    });
}

void vk_gltf_viewer::gui::popup::process() {
    // Call ImGui::OpenPopup for each popup in waitList.
    for (const auto &popup : waitList) {
        std::visit([]<typename Popup>(const Popup&) {
            ImGui::OpenPopup(Popup::name);
        }, popup);
    }

    // Append waitList to opened and make waitList empty.
    opened.splice(std::prev(opened.end(), 1), std::move(waitList));

    for (auto it = opened.begin(); it != opened.end(); ++it) {
        std::visit([&]<typename Popup>(Popup &popup) {
            bool beginPopupResult;
            if constexpr (Popup::modal) {
                // Center the dialog window.
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2 { 0.5f, 0.5f });
                beginPopupResult = ImGui::BeginPopupModal(Popup::name, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            }
            else {
                beginPopupResult = ImGui::BeginPopup(Popup::name);
            }

            if (beginPopupResult) {
                popup.show();
                ImGui::EndPopup();
            }
            else {
                // Popup is closed, remove it from opened.
                it = opened.erase(it);
            }
        }, *it);
    }
}