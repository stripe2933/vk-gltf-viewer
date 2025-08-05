export module vk_gltf_viewer.gui.popup;

import std;
export import cstring_view;
export import imgui;

namespace vk_gltf_viewer::gui::popup {
    export void open(std::string label, std::function<void()> fn, bool modal = false);
    export bool close(std::string_view label) noexcept;

    export void process(cpp_util::cstring_view label, ImGuiWindowFlags flags = {});

    export
    [[nodiscard]] bool isModalPopupOpened() noexcept;
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

struct StringHasher {
    using is_transparent = void;

    [[nodiscard]] static std::size_t operator()(const std::string &s) noexcept {
        return std::hash<std::string>{}(s);
    }

    [[nodiscard]] static std::size_t operator()(std::string_view s) noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct StringKeyEqual {
    using is_transparent = void;

    [[nodiscard]] static bool operator()(const std::string &lhs, const std::string &rhs) noexcept {
        return lhs == rhs;
    }

    [[nodiscard]] static bool operator()(std::string_view lhs, const std::string &rhs) noexcept {
        return lhs == rhs;
    }

    [[nodiscard]] static bool operator()(const std::string &lhs, std::string_view rhs) noexcept {
        return lhs == rhs;
    }
};

std::unordered_map<std::string, std::tuple<std::function<void()>, bool /* ImGui::OpenPopup() called? */, bool /* modal? */>, StringHasher, StringKeyEqual> popups;

void vk_gltf_viewer::gui::popup::open(std::string label, std::function<void()> fn, bool modal) {
    popups.try_emplace(std::move(label), std::move(fn), false, modal);
}

bool vk_gltf_viewer::gui::popup::close(std::string_view label) noexcept {
    // TODO.CXX23: std::unordered_map::erase() with transparent key.
    auto it = popups.find(label);
    if (it == popups.end()) {
        return false;
    }

    popups.erase(it);
    return true;
}

void vk_gltf_viewer::gui::popup::process(cpp_util::cstring_view label, ImGuiWindowFlags flags) {
    const auto it = popups.find(label);
    if (it == popups.end()) {
        return;
    }

    auto &[fn, isOpened, isModal] = it->second;
RETRY:
    bool beginPopupResult;
    if (isModal) {
        // Center the dialog window.
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2 { 0.5f, 0.5f });
        beginPopupResult = ImGui::BeginPopupModal(label.c_str(), nullptr, flags);
    }
    else {
        beginPopupResult = ImGui::BeginPopup(label.c_str(), flags);
    }
    if (beginPopupResult) {
        std::invoke(fn);
        ImGui::EndPopup();
    }
    else if (isOpened) {
        popups.erase(it);
    }
    else {
        ImGui::OpenPopup(label.c_str());
        isOpened = true;
        goto RETRY;
    }
}

bool vk_gltf_viewer::gui::popup::isModalPopupOpened() noexcept {
    return std::ranges::any_of(popups | std::views::values | std::views::elements<2>, std::identity{});
}