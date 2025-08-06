export module vk_gltf_viewer.gui.popup.AnimationCollisionResolver;

import std;

export import vk_gltf_viewer.gltf.AssetExtended;
import vk_gltf_viewer.gui.utils;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.imgui;
import vk_gltf_viewer.helpers.TempStringBuffer;
import vk_gltf_viewer.imgui.UserData;

namespace vk_gltf_viewer::gui::popup {
    export struct AnimationCollisionResolver {
        static constexpr auto name = "Resolve Animation Collision";
        static constexpr bool modal = true;

        std::reference_wrapper<gltf::AssetExtended> assetExtended;
        std::size_t animationIndexToEnable;
        std::map<std::size_t /* animation index  */, std::map<std::size_t /* node index */, Flags<gltf::NodeAnimationUsage>>> collisions;

        void show();
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

using namespace std::string_view_literals;

void vk_gltf_viewer::gui::popup::AnimationCollisionResolver::show() {
    ImGui::TextUnformatted("The animation you're trying to enable is colliding by other enabled animations."sv);

    for (const auto &[i, collisionList] : collisions) {
        if (ImGui::TreeNode(getDisplayName(assetExtended.get().asset.animations, i).c_str())) {
            ImGui::Table<false>(
                "animation-collision-table",
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                collisionList,
                ImGui::ColumnInfo { "Node", decomposer([](std::size_t nodeIndex, auto) {
                    ImGui::Text("%zu", nodeIndex);
                }) },
                ImGui::ColumnInfo { "Path", decomposer([](auto, Flags<gltf::NodeAnimationUsage> usage) {
                    ImGui::TextUnformatted(tempStringBuffer.write("{::s}", usage).view());
                }) });
            ImGui::TreePop();
        }
    }

    ImGui::TextUnformatted("Would you like to disable these animations?"sv);

    ImGui::Separator();

    ImGui::Checkbox("Don't ask me and resolve automatically", &static_cast<imgui::UserData*>(ImGui::GetIO().UserData)->resolveAnimationCollisionAutomatically);

    if (ImGui::Button("Yes")) {
        // Resolve the colliding animations.
        for (std::size_t collidingAnimationIndex : collisions | std::views::keys) {
            assetExtended.get().animations[collidingAnimationIndex].second = false;
        }
        assetExtended.get().animations[animationIndexToEnable].second = true;

        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("No")) {
        ImGui::CloseCurrentPopup();
    }
}