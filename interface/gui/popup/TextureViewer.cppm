export module vk_gltf_viewer.gui.popup.TextureViewer;

import std;

export import vk_gltf_viewer.gltf.AssetExtended;
import vk_gltf_viewer.gui.utils;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.imgui;
import vk_gltf_viewer.helpers.TempStringBuffer;

namespace vk_gltf_viewer::gui::popup {
    export class TextureViewer {
    public:
        static constexpr auto name = "Texture Viewer";
        static constexpr bool modal = false;

        std::reference_wrapper<gltf::AssetExtended> assetExtended;
        std::size_t textureIndex;

        TextureViewer(gltf::AssetExtended &assetExtended, std::size_t textureIndex)
            : assetExtended { assetExtended }
            , textureIndex { textureIndex }
            , selectedImageIndex { getPreferredImageIndex(assetExtended.asset.textures[textureIndex]) /* TODO */ } { }

        void show();

    private:
        std::size_t selectedImageIndex;
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
        if (ImGui::BeginCombo("Image Index", tempStringBuffer.write(selectedImageIndex).view().c_str())) {
            if (texture.imageIndex) {
                // TODO: do not disable this and load the image when the selection is changed.
                ImGui::WithDisabled([&] {
                    ImGui::Selectable(tempStringBuffer.write(*texture.imageIndex).view().c_str(), *texture.imageIndex == selectedImageIndex);
                }, !assetExtended.get().isImageLoaded(*texture.imageIndex));
            }
            if (texture.basisuImageIndex) {
                ImGui::WithDisabled([&] {
                    ImGui::Selectable(tempStringBuffer.write("{} (Basis Universal)", *texture.basisuImageIndex).view().c_str(), *texture.basisuImageIndex == selectedImageIndex);
                }, !assetExtended.get().isImageLoaded(*texture.basisuImageIndex));
            }
            if (texture.ddsImageIndex) {
                ImGui::WithDisabled([&] {
                    ImGui::Selectable(tempStringBuffer.write("{} (DDS)", *texture.ddsImageIndex).view().c_str(), *texture.ddsImageIndex == selectedImageIndex);
                }/*, assetExtended.get().isImageLoaded(*texture.ddsImageIndex)*/ /* TODO: currently application does not handle this format at all */);
            }
            if (texture.webpImageIndex) {
                ImGui::WithDisabled([&] {
                    if (ImGui::Selectable(tempStringBuffer.write("{} (WEBP)", *texture.webpImageIndex).view().c_str(), *texture.webpImageIndex == selectedImageIndex)) {
                        // TODO
                    }
                }/*, assetExtended.get().isImageLoaded(*texture.webpImageIndex)*/ /* TODO: currently application does not handle this format at all */);
            }

            ImGui::EndCombo();
        }
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
                        makeWindowVisible(ImGui::FindWindowByName("Material Editor"));
                        assetExtended.get().imGuiSelectedMaterialIndex.emplace(materialIndex);
                        ImGui::CloseCurrentPopup();
                    }
                });
            }), ImGuiTableColumnFlags_WidthFixed },
            ImGui::ColumnInfo { "Type", decomposer([](auto, Flags<gltf::TextureUsage> type) {
                ImGui::TextUnformatted(tempStringBuffer.write("{::s}", type).view());
            }), ImGuiTableColumnFlags_WidthStretch });
    });
}