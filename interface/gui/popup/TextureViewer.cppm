export module vk_gltf_viewer.gui.popup.TextureViewer;

import std;

export import vk_gltf_viewer.gltf.AssetExtended;
import vk_gltf_viewer.gui.utils;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.imgui;
import vk_gltf_viewer.helpers.TempStringBuffer;

namespace vk_gltf_viewer::gui::popup {
    export struct TextureViewer {
        static constexpr auto name = "Texture Viewer";
        static constexpr bool modal = false;

        std::reference_wrapper<gltf::AssetExtended> assetExtended;
        std::size_t textureIndex;

        void show();
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

void vk_gltf_viewer::gui::popup::TextureViewer::show() {
    const ImVec2 textureSize = assetExtended.get().getTextureSize(textureIndex);
    const float displayRatio = std::max(textureSize.x, textureSize.y) / 256.f;
    ImGui::hoverableImageCheckerboardBackground(assetExtended.get().getTextureID(textureIndex), textureSize / displayRatio, displayRatio);

    ImGui::SameLine();

    ImGui::WithGroup([&] {
        fastgltf::Texture &texture = assetExtended.get().asset.textures[textureIndex];
        ImGui::InputTextWithHint("Name", "<empty>", &texture.name);
        ImGui::LabelText("Image Index", "%zu", getPreferredImageIndex(texture));
        if (texture.samplerIndex) {
            ImGui::LabelText("Sampler Index", "%zu", texture.samplerIndex.value_or(-1));
        }
        else {
            ImGui::WithLabel("Sampler Index", [] {
                ImGui::TextDisabled("-");
                ImGui::SameLine();
                ImGui::HelperMarker("(?)", "Default sampler will be used.");
            });
        }

        ImGui::SeparatorText("Texture used by:");

        ImGui::Table<false>(
            "",
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
            assetExtended.get().textureUsages[textureIndex],
            ImGui::ColumnInfo { "Material", decomposer([&](std::size_t materialIndex, auto) {
                ImGui::WithID(materialIndex, [&] {
                    if (ImGui::TextLink(getDisplayName(assetExtended.get().asset.materials, materialIndex).c_str())) {
                        gui::makeWindowVisible(ImGui::FindWindowByName("Material Editor"));
                        assetExtended.get().imGuiSelectedMaterialIndex.emplace(materialIndex);
                    }
                });
            }), ImGuiTableColumnFlags_WidthFixed },
            ImGui::ColumnInfo { "Type", decomposer([](auto, Flags<gltf::TextureUsage> type) {
                ImGui::TextUnformatted(tempStringBuffer.write("{::s}", type).view());
            }), ImGuiTableColumnFlags_WidthStretch });
    });
}