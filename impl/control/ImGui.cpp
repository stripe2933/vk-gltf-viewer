module;

#include <cassert>
#include <version>

#include <fastgltf/types.hpp>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <nfd.hpp>

module vk_gltf_viewer;
import :control.ImGui;

import std;
import glm;
import :helpers.enum_to_string;
import :helpers.formatters.joiner;
import :helpers.ranges;
import :helpers.tristate;

using namespace std::string_view_literals;

struct cstring_view {
    const char *data;

    cstring_view() noexcept = default;
    cstring_view(const std::string &str) noexcept : data { str.c_str() } { }
    cstring_view(const std::pmr::string &str) noexcept : data { str.c_str() } { }
    cstring_view(const cstring_view&) noexcept = default;
    auto operator=(const cstring_view&) noexcept -> cstring_view& = default;
};

namespace ImGui {
    IMGUI_API bool InputTextWithHint(const char* label, const char *hint, std::pmr::string* str, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr) {
        struct ChainedUserData {
            std::pmr::string*       Str;
            ImGuiInputTextCallback  ChainCallback;
            void*                   ChainCallbackUserData;
        };

        constexpr auto chainCallback = [](ImGuiInputTextCallbackData *data) -> int {
            const auto *userData = static_cast<ChainedUserData*>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                // Resize string callback
                // If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we need to set them back to what we want.
                auto* str = userData->Str;
                IM_ASSERT(data->Buf == str->c_str());
                str->resize(data->BufTextLen);
                data->Buf = const_cast<char*>(str->c_str());
            }
            else if (userData->ChainCallback) {
                // Forward to user callback, if any
                data->UserData = userData->ChainCallbackUserData;
                return userData->ChainCallback(data);
            }
            return 0;
        };

        IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
        flags |= ImGuiInputTextFlags_CallbackResize;

        ChainedUserData chainedUserData {
            .Str = str,
            .ChainCallback = chainCallback,
            .ChainCallbackUserData = userData,
        };
        return InputTextWithHint(label, hint, const_cast<char*>(str->c_str()), str->capacity() + 1, flags, chainCallback, &chainedUserData);
    }

    // https://github.com/ocornut/imgui/pull/6526
    IMGUI_API bool SmallCheckbox(const char* label, bool* v) {
        ImGuiStyle &style = GetStyle();
        const float backup_padding_y = style.FramePadding.y;
        style.FramePadding.y = 0.0f;
        bool pressed = Checkbox(label, v);
        style.FramePadding.y = backup_padding_y;
        return pressed;
    }

    IMGUI_API void TextUnformatted(std::string_view str) {
        Text(str.cbegin(), str.cend());
    }

    template <std::invocable F>
    auto WithLabel(std::string_view label, F &&imGuiFunc)
        requires std::is_void_v<std::invoke_result_t<F>>
    {
        const float x = GetCursorPosX();
        imGuiFunc();
        SameLine();
        SetCursorPosX(x + CalcItemWidth() + GetStyle().ItemInnerSpacing.x);
        TextUnformatted(label);
    }

    template <std::invocable F>
    auto WithLabel(std::string_view label, F &&imGuiFunc) -> std::invoke_result_t<F> {
        const float x = GetCursorPosX();
        auto value = imGuiFunc();
        SameLine();
        SetCursorPosX(x + CalcItemWidth() + GetStyle().ItemInnerSpacing.x);
        TextUnformatted(label);
        return value;
    }
}

/**
 * Return \p str if it is not empty, otherwise return the result of \p fallback.
 * @param str String to check. Must be alive during the lifetime of the returned variant.
 * @param fallback Fallback function to call if \p str is empty.
 * @return A variant that contains either the string view of the original \p str, or string of the result of \p fallback.
 */
[[nodiscard]] auto nonempty_or(
    const std::string &str [[clang::lifetimebound]],
    std::invocable auto &&fallback
) -> std::variant<std::string_view, std::string> {
    if (str.empty()) {
        return std::variant<std::string_view, std::string> { std::in_place_type<std::string>, fallback() };
    }
    return std::variant<std::string_view, std::string> { std::in_place_type<std::string_view>, str };
}

/**
 * Return \p str if it is not empty, otherwise return the result of \p fallback.
 * @param str String to check. Must be alive during the lifetime of the returned variant.
 * @param fallback Fallback function to call if \p str is empty.
 * @return A variant that contains either the string view of the original \p str, or string of the result of \p fallback.
 */
[[nodiscard]] auto nonempty_or(
    const std::pmr::string &str [[clang::lifetimebound]],
    std::invocable auto &&fallback
) -> std::variant<cstring_view, std::string> {
    if (str.empty()) {
        return std::variant<cstring_view, std::string> { std::in_place_type<std::string>, fallback() };
    }
    return std::variant<cstring_view, std::string> { std::in_place_type<cstring_view>, str };
}

/**
 * Visit \p v as \p T, and return the result.
 * @tparam T Visited type.
 * @tparam Ts Types of \p v's alternatives. These types must be convertible to \p T.
 * @param v Variant to visit.
 * @return Visited value.
 * @example
 * @code
 * visit_as<float>(std::variant<int, float>{ 3 }); // Returns 3.f
 * @endcode
 */
template <typename T, std::convertible_to<T>... Ts>
[[nodiscard]] auto visit_as(const std::variant<Ts...> &v) -> T {
    return std::visit([](T x) { return x; }, v);

}

template <std::integral T>
[[nodiscard]] auto to_string(T value) -> const char* {
    static constexpr T MAX_NUM = 4096;
    static const std::vector numStrings
        = std::views::iota(T { 0 }, T { MAX_NUM + 1 })
        | std::views::transform([](T i) { return std::format("{}", i); })
        | std::ranges::to<std::vector>();

    assert(value <= MAX_NUM && "Value is too large");
    return numStrings[value].c_str();
}

auto hoverableImage(vk::DescriptorSet texture, const ImVec2 &size, const ImVec4 &tint = { 1.f, 1.f, 1.f, 1.f}) -> void {
    const ImVec2 texturePosition = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<vk::DescriptorSet::CType>(texture), size, { 0.f, 0.f }, { 1.f, 1.f }, tint);

    if (ImGui::BeginItemTooltip()) {
        const ImGuiIO &io = ImGui::GetIO();

        const ImVec2 zoomedPortionSize = size / 4.f;
        ImVec2 region = io.MousePos - texturePosition - zoomedPortionSize * 0.5f;
        region.x = std::clamp(region.x, 0.f, size.x - zoomedPortionSize.x);
        region.y = std::clamp(region.y, 0.f, size.y - zoomedPortionSize.y);

        constexpr float zoomScale = 4.0f;
        ImGui::Image(static_cast<vk::DescriptorSet::CType>(texture), zoomedPortionSize * zoomScale, region / size, (region + zoomedPortionSize) / size, tint);
        ImGui::Text("Showing: [%.0f, %.0f]x[%.0f, %.0f]", region.x, region.y, region.x + zoomedPortionSize.y, region.y + zoomedPortionSize.y);

        ImGui::EndTooltip();
    }
}

auto assetTextureTransform(const fastgltf::TextureTransform &textureTransform) -> void {
    if (auto rotation = textureTransform.rotation; ImGui::DragFloat("Rotation", &rotation, 0.01f, 0.f, 2.f * std::numbers::pi_v<float>)) {
        // textureTransform.rotation = rotation; // TODO
    }
    if (auto offset = textureTransform.uvOffset; ImGui::DragFloat2("UV offset", offset.data(), 0.01f, 0.f, 1.f)) {
        // textureTransform.uvOffset = offset; // TODO
    }
    if (auto scale = textureTransform.uvScale; ImGui::DragFloat2("UV scale", scale.data(), 0.01f, FLT_MIN, FLT_MAX)) {
        // textureTransform.uvScale = scale; // TODO
    }
}

auto assetTextureInfo(const fastgltf::TextureInfo &textureInfo, std::span<const vk::DescriptorSet> assetTextures, const ImVec4 &tint = { 1.f, 1.f, 1.f, 1.f }) -> void {
    hoverableImage(assetTextures[textureInfo.textureIndex], { 128.f, 128.f }, tint);
    if (int textureIndex = textureInfo.textureIndex; ImGui::Combo("Index", &textureIndex, [](auto*, int i) { return to_string(i); }, nullptr, assetTextures.size())) {
        // textureInfo.textureIndex = textureIndex; // TODO
    }
    ImGui::LabelText("Texture coordinate Index", "%zu", textureInfo.transform && textureInfo.transform->texCoordIndex ? *textureInfo.transform->texCoordIndex : textureInfo.texCoordIndex);

    if (const auto &transform = textureInfo.transform; ImGui::TreeNodeEx("KHR_texture_transform", transform ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        if (transform) {
            assetTextureTransform(*transform);
        }
        else {
            ImGui::BeginDisabled();
            assetTextureTransform({});
            ImGui::EndDisabled();
        }

        ImGui::TreePop();
    }
}

auto assetNormalTextureInfo(const fastgltf::NormalTextureInfo &textureInfo, std::span<const vk::DescriptorSet> assetTextures) -> void {
    hoverableImage(assetTextures[textureInfo.textureIndex], { 128.f, 128.f });
    if (int textureIndex = textureInfo.textureIndex; ImGui::Combo("Index", &textureIndex, [](auto*, int i) { return to_string(i); }, nullptr, assetTextures.size())) {
        // textureInfo.textureIndex = textureIndex; // TODO
    }
    ImGui::LabelText("Texture coordinate Index", "%zu", textureInfo.transform && textureInfo.transform->texCoordIndex ? *textureInfo.transform->texCoordIndex : textureInfo.texCoordIndex);
    if (auto scale = textureInfo.scale; ImGui::DragFloat("Scale", &scale, 0.01f, 0.f, FLT_MAX)) {
        // textureInfo.scale = scale; // TODO
    }

    if (const auto &transform = textureInfo.transform; ImGui::TreeNodeEx("KHR_texture_transform", transform ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        if (transform) {
            assetTextureTransform(*transform);
        }
        else {
            ImGui::BeginDisabled();
            assetTextureTransform({});
            ImGui::EndDisabled();
        }

        ImGui::TreePop();
    }
}

auto assetOcclusionTextureInfo(const fastgltf::OcclusionTextureInfo &textureInfo, std::span<const vk::DescriptorSet> assetTextures) -> void {
    // Occlusion texture is in red channel.
    hoverableImage(assetTextures[textureInfo.textureIndex], { 128.f, 128.f }, { 1.f, 0.f, 0.f, 1.f });
    if (int textureIndex = textureInfo.textureIndex; ImGui::Combo("Index", &textureIndex, [](auto*, int i) { return to_string(i); }, nullptr, assetTextures.size())) {
        // textureInfo.textureIndex = textureIndex; // TODO
    }
    ImGui::LabelText("Texture coordinate Index", "%zu", textureInfo.transform && textureInfo.transform->texCoordIndex ? *textureInfo.transform->texCoordIndex : textureInfo.texCoordIndex);
    if (auto strength = textureInfo.strength; ImGui::DragFloat("Strength", &strength, 0.01f, 0.f, FLT_MAX)) {
        // textureInfo.strength = strength; // TODO
    }

    if (const auto &transform = textureInfo.transform; ImGui::TreeNodeEx("KHR_texture_transform", transform ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        if (transform) {
            assetTextureTransform(*transform);
        }
        else {
            ImGui::BeginDisabled();
            assetTextureTransform({});
            ImGui::EndDisabled();
        }

        ImGui::TreePop();
    }
}

auto vk_gltf_viewer::control::imgui::menuBar() -> void {
    static NFD::Guard nfdGuard;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Load glTF File", "Ctrl+O")) {

            }
            if (ImGui::MenuItem("Close Current File", "Ctrl+W")) {

            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Skybox")) {
            if (ImGui::MenuItem("Load Skybox")) {
                static constexpr std::array filterItems { nfdfilteritem_t { "HDR image", "hdr" } };
                NFD::UniquePath outPath;
                if (nfdresult_t result = OpenDialog(outPath, filterItems.data(), filterItems.size()); result == NFD_OKAY) {
                    std::println("Path: {}", outPath.get());
                }
                else if (result == NFD_CANCEL) {
                    // Do nothing.
                }
                else {
                    ImGui::DebugLog("File dialog error: %s\n", NFD::GetError());
                }
            }

            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

auto vk_gltf_viewer::control::imgui::skybox(AppState &appState) -> void {
    if (ImGui::Begin("Skybox")) {
        const bool useSolidBackground = appState.background.has_value();
        if (ImGui::RadioButton("Use cubemap image from equirectangular map", !useSolidBackground)) {
            appState.background.set_active(false);
        }
        if (ImGui::RadioButton("Use solid color", useSolidBackground)) {
            appState.background.set_active(true);
        }
        if (useSolidBackground) {
            ImGui::ColorPicker3("Color", value_ptr(*appState.background));
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::hdriEnvironments(
    vk::DescriptorSet eqmapTexture,
    AppState &appState
) -> void {
    if (ImGui::Begin("HDRI environments info") && appState.imageBasedLightingProperties) {
        const auto &iblProps = *appState.imageBasedLightingProperties;

        ImGui::SeparatorText("Equirectangular map");

        const float eqmapAspectRatio = static_cast<float>(iblProps.eqmap.dimension.y) / iblProps.eqmap.dimension.x;
        const ImVec2 eqmapTextureSize = ImVec2 { 1.f, eqmapAspectRatio } * ImGui::GetContentRegionAvail().x;
        hoverableImage(eqmapTexture, eqmapTextureSize);

        ImGui::WithLabel("File"sv, [&]() { ImGui::TextLinkOpenURL(iblProps.eqmap.path.stem().string().c_str(), iblProps.eqmap.path.string().c_str()); });
        ImGui::LabelText("Dimension", "%ux%u", iblProps.eqmap.dimension.x, iblProps.eqmap.dimension.y);

        ImGui::SeparatorText("Cubemap");
        ImGui::LabelText("Size", "%u", iblProps.cubemap.size);

        ImGui::SeparatorText("Diffuse irradiance");
        ImGui::TextUnformatted("Spherical harmonic coefficients (up to 3rd band)"sv);
        if (ImGui::BeginTable("spherical_harmonic_coeffs", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable)) {
            // Headers.
            ImGui::TableSetupColumn("Band");
            ImGui::TableSetupColumn("x");
            ImGui::TableSetupColumn("y");
            ImGui::TableSetupColumn("z");
            ImGui::TableHeadersRow();

            // Rows.
            constexpr std::array bandLabels { "L0"sv, "L1_1"sv, "L10"sv, "L11"sv, "L2_2"sv, "L2_1"sv, "L20"sv, "L21"sv, "L22"sv };
            for (const auto &[label, coefficients] : std::views::zip(bandLabels, iblProps.diffuseIrradiance.sphericalHarmonicCoefficients)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", coefficients.x);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", coefficients.y);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", coefficients.z);
            }

            ImGui::EndTable();
        }

        ImGui::SeparatorText("Prefiltered map");
        ImGui::LabelText("Size", "%u", iblProps.prefilteredmap.size);
        ImGui::LabelText("Roughness levels", "%u", iblProps.prefilteredmap.roughnessLevels);
        ImGui::LabelText("Samples", "%u", iblProps.prefilteredmap.sampleCount);
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::assetInfos(fastgltf::Asset &asset) -> void {
    if (ImGui::Begin("Asset")) {
        if (auto &assetInfo = asset.assetInfo) {
            ImGui::InputTextWithHint("glTF Version", "<empty>", &assetInfo->gltfVersion);
            ImGui::InputTextWithHint("Generator", "<empty>", &assetInfo->generator);
            ImGui::InputTextWithHint("Copyright", "<empty>", &assetInfo->copyright);
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::assetBufferViews(fastgltf::Asset &asset) -> void {
    if (ImGui::Begin("Buffer Views")) {
        if (ImGui::BeginTable("gltf-buffer-views-table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Buffer", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Stride", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (const auto &[i, bufferView] : asset.bufferViews | ranges::views::enumerate) {
                ImGui::TableNextRow();
                ImGui::AlignTextToFramePadding();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);
                ImGui::PushID(i);
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##name", "<empty>", &bufferView.name);
                ImGui::TableSetColumnIndex(2);
                if (ImGui::TextLink(visit_as<cstring_view>(nonempty_or(asset.buffers[bufferView.bufferIndex].name, [&] { return std::format("<Unnamed buffer {}>", bufferView.bufferIndex); })).data)) {
                    // TODO
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%zu", bufferView.byteOffset);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%zu", bufferView.byteLength);
                ImGui::TableSetColumnIndex(5);
                if (const auto &byteStride = bufferView.byteStride) {
                    ImGui::Text("%zu", *byteStride);
                }
                else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(6);
                if (const auto &bufferViewTarget = bufferView.target) {
                    ImGui::TextUnformatted(to_string(*bufferViewTarget));
                }
                else {
                    ImGui::TextDisabled("-");
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::assetBuffers(fastgltf::Asset &asset, const std::filesystem::path &assetDir) -> void {
    if (ImGui::Begin("Buffers")) {
        if (ImGui::BeginTable("gltf-buffers-table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("MIME", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (const auto &[i, buffer] : asset.buffers | ranges::views::enumerate) {
                ImGui::TableNextRow();
                ImGui::AlignTextToFramePadding();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##name", "<empty>", &buffer.name);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%zu", buffer.byteLength);
                ImGui::TableSetColumnIndex(3);
                visit(fastgltf::visitor {
                    [](const auto &source) requires requires { source.mimeType -> fastgltf::MimeType; } {
                        ImGui::TextUnformatted(to_string(source.mimeType));
                    },
                    [](const auto&) {
                        ImGui::TextDisabled("-");
                    },
                }, buffer.data);
                ImGui::TableSetColumnIndex(4);
                visit(fastgltf::visitor {
                    [](const fastgltf::sources::Array&) {
                        ImGui::TextUnformatted("Embedded (Array)"sv);
                    },
                    [](const fastgltf::sources::BufferView &bufferView) {
                        ImGui::Text("BufferView (%zu)", bufferView.bufferViewIndex);
                    },
                    [&](const fastgltf::sources::URI &uri) {
                        ImGui::TextLinkOpenURL(uri.uri.fspath().stem().string().c_str(), (assetDir / uri.uri.fspath()).string().c_str());
                    },
                    [](const auto&) {
                        ImGui::TextDisabled("-");
                    }
                }, buffer.data);
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::assetImages(fastgltf::Asset &asset, const std::filesystem::path &assetDir) -> void {
    if (ImGui::Begin("Images")) {
        if (ImGui::BeginTable("gltf-images-table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("MIME", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (const auto &[i, image] : asset.images | ranges::views::enumerate) {
                ImGui::TableNextRow();
                ImGui::AlignTextToFramePadding();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##name", "<empty>", &image.name);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(2);
                visit(fastgltf::visitor {
                    [](const auto &source) requires requires { source.mimeType -> fastgltf::MimeType; } {
                        ImGui::TextUnformatted(to_string(source.mimeType));
                    },
                    [](const auto&) {
                        ImGui::TextDisabled("-");
                    },
                }, image.data);
                ImGui::TableSetColumnIndex(3);
                visit(fastgltf::visitor {
                    [](const fastgltf::sources::Array&) {
                        ImGui::TextUnformatted("Embedded (Array)"sv);
                    },
                    [](const fastgltf::sources::BufferView &bufferView) {
                        ImGui::Text("BufferView (%zu)", bufferView.bufferViewIndex);
                    },
                    [&](const fastgltf::sources::URI &uri) {
                        ImGui::TextLinkOpenURL(uri.uri.fspath().stem().string().c_str(), (assetDir / uri.uri.fspath()).string().c_str());
                    },
                    [](const auto&) {
                        ImGui::TextDisabled("-");
                    }
                }, image.data);
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::assetSamplers(fastgltf::Asset &asset) -> void {
    if (ImGui::Begin("Samplers")) {
        if (ImGui::BeginTable("gltf-samplers-table", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Mag filter", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Min filter", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Wrap S", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Wrap T", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (const auto &[i, sampler] : asset.samplers | ranges::views::enumerate) {
                ImGui::TableNextRow();
                ImGui::AlignTextToFramePadding();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##name", "<empty>", &sampler.name);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(2);
                if (sampler.magFilter) {
                    ImGui::TextUnformatted(to_string(*sampler.magFilter));
                }
                else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(3);
                if (sampler.minFilter) {
                    ImGui::TextUnformatted(to_string(*sampler.minFilter));
                }
                else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(to_string(sampler.wrapS));
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(to_string(sampler.wrapT));
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::assetMaterials(fastgltf::Asset &asset, std::span<const vk::DescriptorSet> assetTextures) -> void {
    if (ImGui::Begin("Materials")) {
        static int materialIndex = asset.materials.empty() ? -1 : 0;
        if (ImGui::BeginCombo("Material", materialIndex == -1 ? "<empty>" : visit_as<cstring_view>(nonempty_or(asset.materials[materialIndex].name, [&] { return std::format("<Unnamed material {}>", materialIndex); })).data)) {
            for (const auto &[i, material] : asset.materials | ranges::views::enumerate) {
                const bool isSelected = i == materialIndex;
                if (ImGui::Selectable(visit_as<cstring_view>(nonempty_or(material.name, [&] { return std::format("<Unnamed material {}>", i); })).data, isSelected)) {
                    materialIndex = i;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        if (materialIndex != -1) {
            fastgltf::Material &material = asset.materials[materialIndex];

            ImGui::InputTextWithHint("Name", "<empty>", &material.name);

            if (auto doubleSided = material.doubleSided; ImGui::Checkbox("Double sided", &doubleSided)) {
                // material.doubleSided = doubleSided; // TODO
            }

            constexpr std::array alphaModes { "Opaque", "Mask", "Blend" };
            if (int alphaMode = static_cast<int>(material.alphaMode); ImGui::Combo("Alpha mode", &alphaMode, alphaModes.data(), alphaModes.size())) {
                // material.alphaMode = static_cast<fastgltf::AlphaMode>(alphaMode); // TODO
            }

            if (ImGui::TreeNodeEx("Physically Based Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::TreeNodeEx("Base Color", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (auto baseColorFactor = material.pbrData.baseColorFactor; ImGui::DragFloat4("Factor", baseColorFactor.data(), 0.01f, 0.f, 1.f)) {
                        // material.pbrData.baseColorFactor = baseColorFactor; // TODO
                    }

                    if (const auto &texture = material.pbrData.baseColorTexture; ImGui::TreeNodeEx("Texture", texture ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                        if (texture) {
                            assetTextureInfo(*texture, assetTextures);
                        }
                        else {
                            ImGui::BeginDisabled();
                            assetTextureInfo({}, assetTextures);
                            ImGui::EndDisabled();
                        }

                        ImGui::TreePop();
                    }

                    ImGui::TreePop();
                }
                if (ImGui::TreeNodeEx("Metallic/Roughness", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (auto factor = material.pbrData.metallicFactor; ImGui::DragFloat("Factor (Metallic)", &factor, 0.01f, 0.f, 1.f)) {
                        // material.pbrData.metallicFactor = factor; // TODO
                    }
                    if (auto factor = material.pbrData.roughnessFactor; ImGui::DragFloat("Factor (roughness)", &factor, 0.01f, 0.f, 1.f)) {
                        // material.pbrData.roughnessFactor = factor; // TODO
                    }

                    if (const auto &texture = material.pbrData.metallicRoughnessTexture; ImGui::TreeNodeEx("Texture", texture ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                        // Metallic/roughness textures are in the blue/green channels, respectively.
                        if (texture) {
                            assetTextureInfo(*texture, assetTextures, { 0.f, 1.f, 1.f, 1.f });
                        }
                        else {
                            ImGui::BeginDisabled();
                            assetTextureInfo({}, assetTextures, { 0.f, 1.f, 1.f, 1.f });
                            ImGui::EndDisabled();
                        }

                        ImGui::TreePop();
                    }

                    ImGui::TreePop();
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Emissive", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (auto factor = material.emissiveFactor; ImGui::DragFloat3("Factor", factor.data(), 0.01f, 0.f, FLT_MAX)) {
                    // material.emissiveFactor = factor; // TODO
                }

                if (const auto &texture = material.emissiveTexture; ImGui::TreeNodeEx("Texture", texture ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                    if (texture) {
                        assetTextureInfo(*texture, assetTextures);
                    }
                    else {
                        ImGui::BeginDisabled();
                        assetTextureInfo({}, assetTextures);
                        ImGui::EndDisabled();
                    }

                    ImGui::TreePop();
                }

                if (auto isExtensionUsed = std::ranges::find(asset.extensionsUsed, "KHR_materials_emissive_strength") != asset.extensionsUsed.end(); ImGui::TreeNodeEx("KHR_materials_emissive_strength", isExtensionUsed ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                    if (auto strength = material.emissiveStrength; ImGui::DragFloat("Strength", &strength, 0.01f, 0.f, FLT_MAX)) {
                        // material.emissiveStrength = strength; // TODO
                    }

                    ImGui::TreePop();
                }

                ImGui::TreePop();
            }

            if (const auto &texture = material.normalTexture; ImGui::TreeNodeEx("Normal Mapping", texture ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                if (texture) {
                    assetNormalTextureInfo(*texture, assetTextures);
                }
                else {
                    ImGui::BeginDisabled();
                    assetNormalTextureInfo({}, assetTextures);
                    ImGui::EndDisabled();
                }
                ImGui::TreePop();
            }

            if (const auto &texture = material.occlusionTexture; ImGui::TreeNodeEx("Occlusion Mapping", texture ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                if (texture) {
                    assetOcclusionTextureInfo(*texture, assetTextures);
                }
                else {
                    ImGui::BeginDisabled();
                    assetOcclusionTextureInfo({}, assetTextures);
                    ImGui::EndDisabled();
                }
                ImGui::TreePop();
            }

            if (auto isExtensionUsed = std::ranges::find(asset.extensionsUsed, "KHR_materials_unlit") != asset.extensionsUsed.end(); ImGui::TreeNodeEx("KHR_materials_unlit", isExtensionUsed ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                ImGui::BeginDisabled(!isExtensionUsed);
                if (auto unlit = material.unlit; ImGui::Checkbox("Unlit", &unlit)) {
                    // material.unlit = unlit; // TODO
                }
                ImGui::EndDisabled();

                ImGui::TreePop();
            }
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::assetSceneHierarchies(const fastgltf::Asset &asset, AppState &appState) -> void {
    if (ImGui::Begin("Scene hierarchies")) {
        static int sceneIndex = asset.defaultScene.value_or(0);
        if (ImGui::BeginCombo("Scene", visit_as<cstring_view>(nonempty_or(asset.scenes[sceneIndex].name, [&] { return std::format("<Unnamed scene {}>", sceneIndex); })).data)) {
            for (const auto &[i, scene] : asset.scenes | ranges::views::enumerate) {
                const bool isSelected = i == sceneIndex;
                if (ImGui::Selectable(visit_as<cstring_view>(nonempty_or(scene.name, [&] { return std::format("<Unnamed scene {}>", i); })).data, isSelected)) {
                    sceneIndex = i;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        static bool mergeSingleChildNodes = true;
        ImGui::Checkbox("Merge single child nodes", &mergeSingleChildNodes);

        static std::vector<std::optional<bool>> visibilities(asset.nodes.size(), true);
        static std::vector parentNodeIndices = [&]() {
            std::vector<std::size_t> parentNodeIndices(asset.nodes.size());
            for (std::size_t i = 0; i < asset.nodes.size(); ++i) {
                for (std::size_t childIndex : asset.nodes[i].children) {
                    parentNodeIndices[childIndex] = i;
                }
            }
            return parentNodeIndices;
        }();

        static bool useTristateVisibility = true;
        if (ImGui::Checkbox("Use tristate visibility", &useTristateVisibility)) {
            if (useTristateVisibility) {
                // TODO: how to handle this?
            }
            else {
                // If tristate visibility disabled, all indeterminate visibilities are set to true.
                for (auto &visibility : visibilities) {
                    if (!visibility) {
                        visibility = true;
                    }
                }
            }
        }

        // TODO.CXX23: const fastgltf::Asset& doesn't have to be passed, but it does since Clang 18's explicit parameter object
        //  bug. Remove it when it fixed.
        const auto addChildNode = [&](this const auto &self, const fastgltf::Asset &asset, std::size_t nodeIndex) -> void {
            std::size_t descendentNodeIndex = nodeIndex;

            std::vector<std::string> directDescendentNodeNames;
            while (true) {
                const fastgltf::Node &descendentNode = asset.nodes[descendentNodeIndex];
                if (const auto &descendentNodeName = descendentNode.name; descendentNodeName.empty()) {
                    directDescendentNodeNames.emplace_back(std::format("<Unnamed node {}>", descendentNodeIndex));
                }
                else {
                    directDescendentNodeNames.emplace_back(descendentNodeName);
                }

                if (!mergeSingleChildNodes || descendentNode.children.size() != 1) {
                    break;
                }

                descendentNodeIndex = descendentNode.children[0];
            }

            const fastgltf::Node &descendentNode = asset.nodes[descendentNodeIndex];

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            const ImGuiTreeNodeFlags flags
                = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanTextWidth | ImGuiTreeNodeFlags_OpenOnArrow
                | (appState.selectedNodeIndices.contains(descendentNodeIndex) ? ImGuiTreeNodeFlags_Selected : 0)
                | (descendentNode.children.empty() ? (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet) : 0);
            ImGui::PushID(descendentNodeIndex);
            const bool isTreeNodeOpen = ImGui::TreeNodeEx("", flags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                // TODO: uncommenting this line in Clang causes internal compiler error...
                // appState.selectedNodeIndices.emplace(descendentNodeIndex);
            }

            ImGui::SameLine();
            if (useTristateVisibility) {
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, !visibilities[nodeIndex]);
                if (ImGui::Checkbox("##visibility", &*visibilities[nodeIndex])) {
                    if (!visibilities[nodeIndex]) {
                        visibilities[nodeIndex] = true;
                    }

                    tristate::propagateTopDown([&](auto i) { return std::span { asset.nodes[i].children }; }, nodeIndex, visibilities);
                    tristate::propagateBottomUp([&](auto i) { return parentNodeIndices[i]; }, [&](auto i) { return std::span { asset.nodes[i].children }; }, nodeIndex, visibilities);
                }
                ImGui::PopItemFlag();
            }
            else {
                ImGui::Checkbox("##visibility", &*visibilities[nodeIndex]);
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(std::format("{::s}", make_joiner<" / ">(directDescendentNodeNames)));

            ImGui::TableSetColumnIndex(2);
            visit(fastgltf::visitor {
                [](const fastgltf::TRS &trs) {
                    std::vector<std::string> transformComponents;
                    if (trs.translation != std::array { 0.f, 0.f, 0.f }) {
                        transformComponents.emplace_back(std::format("T{::.2f}", trs.translation));
                    }
                    if (trs.rotation != std::array { 0.f, 0.f, 0.f, 1.f }) {
                        transformComponents.emplace_back(std::format("R{::.2f}", trs.rotation));
                    }
                    if (trs.scale != std::array { 1.f, 1.f, 1.f }) {
                        transformComponents.emplace_back(std::format("S{::.2f}", trs.scale));
                    }

                    if (!transformComponents.empty()) {
                        ImGui::TextUnformatted(std::format("{::s}", make_joiner<" * ">(transformComponents)));
                    }
                },
                [](const fastgltf::Node::TransformMatrix &transformMatrix) {
                    constexpr fastgltf::Node::TransformMatrix identity { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
                    if (transformMatrix != identity) {
                        // TODO.CXX23: use chunk when libc++ support it.
                        ImGui::TextUnformatted(std::format("{:::.2f}", transformMatrix | std::views::chunk_by([i = 0](float, float) mutable { return ++i % 4 != 0; })));
                    }
                },
            }, descendentNode.transform);

            if (const auto &meshIndex = descendentNode.meshIndex) {
                ImGui::TableSetColumnIndex(3);
                if (ImGui::TextLink(visit_as<cstring_view>(nonempty_or(asset.meshes[*meshIndex].name, [&] { return std::format("<Unnamed mesh {}>", *meshIndex); })).data)) {
                    // TODO
                }
            }

            if (const auto &lightIndex = descendentNode.lightIndex) {
                ImGui::TableSetColumnIndex(4);
                if (ImGui::TextLink(visit_as<cstring_view>(nonempty_or(asset.lights[*lightIndex].name, [&] { return std::format("<Unnamed light {}>", *lightIndex); })).data)) {
                    // TODO
                }
            }

            if (const auto &cameraIndex = descendentNode.cameraIndex) {
                ImGui::TableSetColumnIndex(5);
                if (ImGui::TextLink(visit_as<cstring_view>(nonempty_or(asset.cameras[*cameraIndex].name, [&] { return std::format("<Unnamed camera {}>", *cameraIndex); })).data)) {
                    // TODO
                }
            }

            if (isTreeNodeOpen) {
                for (std::size_t childNodeIndex : descendentNode.children) {
                    self(asset, childNodeIndex);
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        };

        appState.renderingNodeIndices
            = visibilities
            | ranges::views::enumerate
            | std::views::filter([](const auto &pair) { return pair.second.value_or(false); })
            | std::views::keys
            | std::ranges::to<std::unordered_set<std::size_t>>();

        const fastgltf::Scene &scene = asset.scenes[sceneIndex];
        if (ImGui::BeginTable("scene-hierarchy-table", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##node", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoResize);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Transform", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Light", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Camera", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (std::size_t nodeIndex : scene.nodeIndices) {
                addChildNode(asset, nodeIndex);
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::nodeInspector(
    fastgltf::Asset &asset,
    AppState &appState
) -> void {
    if (ImGui::Begin("Node inspector")) {
        if (appState.selectedNodeIndices.empty()) {
            ImGui::TextUnformatted("No node selected.");
        }
        else if (appState.selectedNodeIndices.size() == 1) {
            fastgltf::Node &node = asset.nodes[*appState.selectedNodeIndices.begin()];
            ImGui::InputTextWithHint("Name", "<empty>", &node.name);

            ImGui::SeparatorText("Transform");

            if (bool isTrs = holds_alternative<fastgltf::TRS>(node.transform); ImGui::BeginCombo("Local transform", isTrs ? "TRS" : "Transform Matrix")) {
                if (ImGui::Selectable("TRS", isTrs)) {
                    std::array<float, 3> translation, scale;
                    std::array<float, 4> rotation;
                    fastgltf::decomposeTransformMatrix(get<fastgltf::Node::TransformMatrix>(node.transform), scale, rotation, translation);

                    node.transform = fastgltf::TRS { translation, rotation, scale };
                }
                if (ImGui::Selectable("Transform Matrix", !isTrs)) {
                    const auto &trs = get<fastgltf::TRS>(node.transform);
                    const glm::mat4 matrix = glm::translate(glm::mat4 { 1.f }, glm::make_vec3(trs.translation.data())) * glm::mat4_cast(glm::make_quat(trs.rotation.data())) * glm::scale(glm::mat4 { 1.f }, glm::make_vec3(trs.scale.data()));

                    fastgltf::Node::TransformMatrix transform;
                    std::copy_n(value_ptr(matrix), 16, transform.data());
                    node.transform = transform;
                }
                ImGui::EndCombo();
            }
            std::visit(fastgltf::visitor {
                [](fastgltf::TRS trs) {
                    // | operator cannot be chained, because of the short circuit evaulation.
                    bool transformChanged = ImGui::DragFloat3("Translation", trs.translation.data());
                    transformChanged |= ImGui::DragFloat4("Rotation", trs.rotation.data());
                    transformChanged |= ImGui::DragFloat3("Scale", trs.scale.data());

                    if (transformChanged) {
                        // TODO.
                    }
                },
                [](fastgltf::Node::TransformMatrix matrix) {
                    // | operator cannot be chained, because of the short circuit evaulation.
                    bool transformChanged = ImGui::DragFloat4("Column 0", &matrix[0]);
                    transformChanged |= ImGui::DragFloat4("Column 1", &matrix[4]);
                    transformChanged |= ImGui::DragFloat4("Column 2", &matrix[8]);
                    transformChanged |= ImGui::DragFloat4("Column 3", &matrix[12]);

                    if (transformChanged) {
                        // TODO.
                    }
                },
            }, node.transform);

            /*ImGui::TextUnformatted("Global transform");
            const glm::mat4 &globalTransform = nodeGlobalTransforms[*selectedNodeIndices.begin()][0];
            INDEX_SEQ(Is, 4, {
                (ImGui::Text("Column %zu: (%.2f, %.2f, %.2f, %.2f)",
                    Is, globalTransform[Is].x, globalTransform[Is].y, globalTransform[Is].z, globalTransform[Is].w
                ), ...);
            });*/

            if (ImGui::BeginTabBar("node-tab-bar")) {
                if (node.meshIndex && ImGui::BeginTabItem("Mesh")) {
                    fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                    ImGui::InputTextWithHint("Name", "<empty>", &mesh.name);

                    for (auto &&[primitiveIndex, primitive]: mesh.primitives | ranges::views::enumerate) {
                        if (ImGui::CollapsingHeader(std::format("Primitive {}", primitiveIndex).c_str())) {
                            if (int type = static_cast<int>(primitive.type); ImGui::Combo("Type", &type, [](auto*, int i) { return to_string(static_cast<fastgltf::PrimitiveType>(i)); }, nullptr, 7)) {
                                // TODO.
                            }
                            if (primitive.materialIndex) {
                                ImGui::PushID(*primitive.materialIndex);
                                if (ImGui::WithLabel("Material"sv, [&]() { return ImGui::TextLink(visit_as<cstring_view>(nonempty_or(asset.materials[*primitive.materialIndex].name, [&] { return std::format("<Unnamed material {}>", *primitive.materialIndex); })).data); })) {
                                    // TODO.
                                }
                                ImGui::PopID();
                            }
                            else {
                                ImGui::BeginDisabled();
                                ImGui::LabelText("Material", "-");
                                ImGui::EndDisabled();
                            }

                            static int floatingPointPrecision = 2;
                            if (ImGui::BeginTable("attributes-table", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit)) {
                                // Headers.
                                ImGui::TableSetupColumn("Attribute");
                                ImGui::TableSetupColumn("Type");
                                ImGui::TableSetupColumn("ComponentType");
                                ImGui::TableSetupColumn("Count");
                                ImGui::TableSetupColumn("Bound");
                                ImGui::TableSetupColumn("Normalized");
                                ImGui::TableSetupColumn("Sparse");
                                ImGui::TableSetupColumn("BufferViewIndex");
                                ImGui::TableHeadersRow();

                                // Rows.
                                constexpr auto addRow = [](std::string_view attributeName, const fastgltf::Accessor &accessor) {
                                    ImGui::PushID(attributeName.cbegin(), attributeName.cend());

                                    ImGui::TableNextRow();

                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::TextUnformatted(attributeName);

                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::TextUnformatted(to_string(accessor.type));

                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::TextUnformatted(to_string(accessor.componentType));

                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::Text("%zu", accessor.count);

                                    ImGui::TableSetColumnIndex(4);
                                    std::visit(fastgltf::visitor {
                                        [](const std::pmr::vector<int64_t> &min, const std::pmr::vector<int64_t> &max) {
                                            assert(min.size() == max.size() && "Different min/max dimension");
                                            if (min.size() == 1) ImGui::Text("[%lld, %lld]", min[0], max[0]);
#if __cpp_lib_format_ranges >= 202207L
                                            else ImGui::TextUnformatted(std::format("{}x{}", min, max));
#else
                                            else if (min.size() == 2) ImGui::Text("[%lld, %lld]x[%lld, %lld]", min[0], min[1], max[0], max[1]);
                                            else if (min.size() == 3) ImGui::Text("[%lld, %lld, %lld]x[%lld, %lld, %lld]", min[0], min[1], min[2], max[0], max[1], max[2]);
                                            else if (min.size() == 4) ImGui::Text("[%lld, %lld, %lld, %lld]x[%lld, %lld, %lld, %lld]", min[0], min[1], min[2], min[3], max[0], max[1], max[2], max[3]);
                                            else assert(false && "Unsupported min/max dimension");
#endif
                                        },
                                        [](const std::pmr::vector<double> &min, const std::pmr::vector<double> &max) {
                                            assert(min.size() == max.size() && "Different min/max dimension");
                                            if (min.size() == 1) ImGui::Text("[%.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, max[0]);
#if __cpp_lib_format_ranges >= 202207L
                                            else ImGui::TextUnformatted(std::format("{0::.{2}f}x{1::.{2}f}", min, max, floatingPointPrecision));
#else
                                            else if (min.size() == 2) ImGui::Text("[%.*lf, %.*lf]x[%.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, min[1], floatingPointPrecision, max[0], floatingPointPrecision, max[1]);
										    else if (min.size() == 3) ImGui::Text("[%.*lf, %.*lf, %.*lf]x[%.*lf, %.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, min[1], floatingPointPrecision, min[2], floatingPointPrecision, max[0], floatingPointPrecision, max[1], floatingPointPrecision, max[2]);
										    else if (min.size() == 4) ImGui::Text("[%.*lf, %.*lf, %.*lf, %.*lf]x[%.*lf, %.*lf, %.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, min[1], floatingPointPrecision, min[2], min[1], floatingPointPrecision, min[3], floatingPointPrecision, max[0], floatingPointPrecision, max[1], floatingPointPrecision, max[2], floatingPointPrecision, max[3]);
										    else assert(false && "Unsupported min/max dimension");
#endif
                                        },
                                        [](const auto&...) {
                                            ImGui::TextUnformatted("-"sv);
                                        }
                                    }, accessor.min, accessor.max);

                                    ImGui::TableSetColumnIndex(5);
                                    ImGui::TextUnformatted(accessor.normalized ? "Yes"sv : "No"sv);

                                    ImGui::TableSetColumnIndex(6);
                                    ImGui::TextUnformatted(accessor.sparse ? "Yes"sv : "No"sv);

                                    ImGui::TableSetColumnIndex(7);
                                    if (accessor.bufferViewIndex) {
                                        if (ImGui::TextLink(::to_string(*accessor.bufferViewIndex))) {
                                            // TODO.
                                        }
                                    }
                                    else {
                                        ImGui::TextDisabled("-");
                                    }

                                    ImGui::PopID();
                                };
                                if (primitive.indicesAccessor) {
                                    addRow("Index"sv, asset.accessors[*primitive.indicesAccessor]);
                                }
                                for (const auto &[attributeName, accessorIndex] : primitive.attributes) {
                                    addRow(attributeName, asset.accessors[accessorIndex]);
                                }
                                ImGui::EndTable();
                            }

                            if (ImGui::InputInt("Bound fp precision", &floatingPointPrecision)) {
                                floatingPointPrecision = std::clamp(floatingPointPrecision, 0, 9);
                            }
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (node.cameraIndex && ImGui::BeginTabItem("Camera")) {
                    fastgltf::Camera &camera = asset.cameras[*node.cameraIndex];
                    ImGui::InputTextWithHint("Name", "<empty>", &camera.name);
                    ImGui::EndTabItem();
                }
                if (node.lightIndex && ImGui::BeginTabItem("Light")) {
                    fastgltf::Light &light = asset.lights[*node.lightIndex];
                    ImGui::InputTextWithHint("Name", "<empty>", &light.name);
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        else {
            ImGui::TextUnformatted("Multiple nodes selected.");
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::inputControlSetting(
    AppState &appState
) -> void {
	if (ImGui::Begin("Input control")){
	    ImGui::SeparatorText("Camera");

	    ImGui::DragFloat3("Position", value_ptr(appState.camera.position), 0.1f);
	    if (ImGui::DragFloat3("Direction", value_ptr(appState.camera.direction), 0.1f, -1.f, 1.f)) {
	        appState.camera.direction = normalize(appState.camera.direction);
	    }
	    if (ImGui::DragFloat3("Up", value_ptr(appState.camera.up), 0.1f, -1.f, 1.f)) {
            appState.camera.up = normalize(appState.camera.up);
        }

	    if (float fovInDegree = glm::degrees(appState.camera.fov); ImGui::DragFloat("FOV", &fovInDegree, 0.1f, 15.f, 120.f, "%.2f deg")) {
	        appState.camera.fov = glm::radians(fovInDegree);
	    }
	    ImGui::DragFloatRange2("Near/Far", &appState.camera.zMin, &appState.camera.zMax, 1e-6f, 1e-4f, 1e6, "%.2e", nullptr, ImGuiSliderFlags_Logarithmic);

	    ImGui::SeparatorText("Node selection");

	    bool showHoveringNodeOutline = appState.hoveringNodeOutline.has_value();
	    if (ImGui::Checkbox("Hovering node outline", &showHoveringNodeOutline)) {
            appState.hoveringNodeOutline.set_active(showHoveringNodeOutline);
	    }
	    if (showHoveringNodeOutline){
	        ImGui::DragFloat("Thickness##hoveringNodeOutline", &appState.hoveringNodeOutline->thickness, 1.f, 1.f, FLT_MAX);
            ImGui::ColorEdit4("Color##hoveringNodeOutline", value_ptr(appState.hoveringNodeOutline->color));
	    }

	    bool showSelectedNodeOutline = appState.selectedNodeOutline.has_value();
	    if (ImGui::Checkbox("Selected node outline", &showSelectedNodeOutline)) {
            appState.selectedNodeOutline.set_active(showSelectedNodeOutline);
	    }
	    if (showSelectedNodeOutline){
	        ImGui::DragFloat("Thickness##selectedNodeOutline", &appState.selectedNodeOutline->thickness, 1.f, 1.f, FLT_MAX);
            ImGui::ColorEdit4("Color##selectedNodeOutline", value_ptr(appState.selectedNodeOutline->color));
	    }
	}
	ImGui::End();
}

auto vk_gltf_viewer::control::imgui::viewManipulate(
    AppState &appState,
    const ImVec2 &passthruRectBR
) -> void {
	constexpr ImVec2 size { 64.f, 64.f };
	constexpr ImU32 background = 0x00000000; // Transparent.
	const glm::mat4 oldView = appState.camera.getViewMatrix();
	glm::mat4 newView = oldView;
	ImGuizmo::ViewManipulate(value_ptr(newView), length(appState.camera.position), passthruRectBR - size, size, background);

	if (newView != oldView) {
	    const glm::mat4 inverseView = inverse(newView);
	    appState.camera.position = inverseView[3];
	    appState.camera.direction = -inverseView[2];
	}
}