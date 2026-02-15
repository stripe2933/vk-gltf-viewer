module;

#include <cassert>

export module vk_gltf_viewer.imgui:widget;

import std;
export import cstring_view;
export import imgui;
import imgui.internal;

import :GuiTextures;
import :util;

#define INDEX_SEQ(Is, N, ...) [&]<auto... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vk_gltf_viewer::imgui::widget {
    export template <typename Allocator>
    bool InputTextWithHint(cpp_util::cstring_view label, cpp_util::cstring_view hint, std::basic_string<char, std::char_traits<char>, Allocator>* str, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr) {
        struct ChainedUserData {
            std::basic_string<char, std::char_traits<char>, Allocator> *Str;
            ImGuiInputTextCallback ChainCallback;
            void *ChainCallbackUserData;
        };

        constexpr auto chainCallback = [](ImGuiInputTextCallbackData *data) -> int {
            const auto *userData = static_cast<ChainedUserData*>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                // Resize string callback
                // If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we need to set them back to what we want.
                auto* str = userData->Str;
                assert(data->Buf == str->c_str());
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

        assert((flags & ImGuiInputTextFlags_CallbackResize) == 0);
        flags |= ImGuiInputTextFlags_CallbackResize;

        ChainedUserData chainedUserData {
            .Str = str,
            .ChainCallback = callback,
            .ChainCallbackUserData = userData,
        };
        return ImGui::InputTextWithHint(label.c_str(), hint.c_str(), str->data(), str->capacity() + 1, flags, chainCallback, &chainedUserData);
    }

    // https://github.com/ocornut/imgui/pull/6526
    export bool SmallCheckbox(cpp_util::cstring_view label, bool* v) {
        ImGuiStyle &style = ImGui::GetStyle();
        const float backup_padding_y = style.FramePadding.y;
        style.FramePadding.y = 0.0f;
        bool pressed = ImGui::Checkbox(label.c_str(), v);
        style.FramePadding.y = backup_padding_y;
        return pressed;
    }

    export void TextUnformatted(std::string_view str) {
        ImGui::TextUnformatted(str.data(), str.data() + str.size());
    }

    export void HelperMarker(cpp_util::cstring_view label, std::string_view description) {
        ImGui::TextDisabled("%s", label.c_str());
        if (ImGui::BeginItemTooltip()) {
            TextUnformatted(description);
            ImGui::EndTooltip();
        }
    }

    export bool ImageButtonWithText(std::string_view str_id, ImTextureRef tex_ref, std::string_view text, const ImVec2 &size, const ImVec2 &imageDisplaySize, const GuiTextures &guiTextures, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& bg_col = ImVec4(0, 0, 0, 0), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), ImGuiButtonFlags flags = 0) {
        ImGuiStyle &style = ImGui::GetStyle();
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const ImGuiID id = window->GetID(str_id.data(), str_id.data() + str_id.size());

        const ImVec2 padding = style.FramePadding;
        const ImRect bb(window->DC.CursorPos, { window->DC.CursorPos.x + size.x + padding.x * 2.0f, window->DC.CursorPos.y + size.y + padding.y * 2.0f + ImGui::GetTextLineHeight() });
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

        // Render
        const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        ImGui::RenderNavCursor(bb, id);
        ImGui::RenderFrame(bb.Min, bb.Max, col, true, std::clamp(std::min(padding.x, padding.y), 0.0f, style.FrameRounding));

        const ImVec2 p_min { bb.Min.x + padding.x, bb.Min.y + padding.y };
        const ImVec2 p_max { bb.Max.x - padding.x, bb.Max.y - padding.y - ImGui::GetTextLineHeight() };
        if (bg_col.w > 0.0f)
            window->DrawList->AddRectFilled(p_min, p_max, ImGui::GetColorU32(bg_col));
        const ImVec2 letterBoxOffset = ImVec2 { 0.5f, 0.5f } * (size - imageDisplaySize);
        window->DrawList->AddImage(guiTextures.getCheckerboardTextureID(), p_min + letterBoxOffset, p_max - letterBoxOffset, size * uv0 / 16.f, size * uv1 / 16.f, ImGui::GetColorU32(tint_col));
        window->DrawList->AddImage(tex_ref, p_min + letterBoxOffset, p_max - letterBoxOffset, uv0, uv1, ImGui::GetColorU32(tint_col));
        window->DrawList->PushClipRect({ p_min.x, p_max.y }, { p_max.x, p_max.y + ImGui::GetTextLineHeight() }, true);
        window->DrawList->AddText({ p_min.x, p_max.y }, ImGui::GetColorU32(ImGuiCol_Text), text.data(), text.data() + text.size());
        window->DrawList->PopClipRect();

        return pressed;
    }

    export void ImageCheckerboardBackground(ImTextureRef tex_ref, const ImVec2 &size, const GuiTextures &guiTextures, const ImVec2 &uv0 = {}, const ImVec2 &uv1 = { 1, 1 }) {
        const ImVec2 texturePosition = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemAllowOverlap();
        ImGui::Image(guiTextures.getCheckerboardTextureID(), size, size * uv0 / 16.f, size * uv1 / 16.f);
        ImGui::SetCursorScreenPos(texturePosition);
        ImGui::Image(tex_ref, size, uv0, uv1);
    }

    export void HoverableImage(ImTextureRef tex_ref, const ImVec2 &displaySize, float displayRatio) {
        const ImVec2 texturePosition = ImGui::GetCursorScreenPos();
        ImGui::Image(tex_ref, displaySize);

        if (ImGui::BeginItemTooltip()) {
            float magnifierSize = std::max(displaySize.x, displaySize.y) / 4.f;
            magnifierSize = std::min({ magnifierSize, displaySize.x, displaySize.y });
            const float halfMagnifierSize = magnifierSize / 2.f;

            ImVec2 magnifierRectMin = ImGui::GetIO().MousePos - texturePosition - ImVec2 { halfMagnifierSize, halfMagnifierSize };
            ImVec2 magnifierRectMax = magnifierRectMin + ImVec2 { magnifierSize, magnifierSize };

            if (magnifierRectMin.x < 0.f) {
                magnifierRectMax.x -= magnifierRectMin.x;
                magnifierRectMin.x = 0.f;
            }
            else if (magnifierRectMax.x > displaySize.x) {
                magnifierRectMin.x -= magnifierRectMax.x - displaySize.x;
                magnifierRectMax.x = displaySize.x;
            }

            if (magnifierRectMin.y < 0.f) {
                magnifierRectMax.y -= magnifierRectMin.y;
                magnifierRectMin.y = 0.f;
            }
            else if (magnifierRectMax.y > displaySize.y) {
                magnifierRectMin.y -= magnifierRectMax.y - displaySize.y;
                magnifierRectMax.y = displaySize.y;
            }

            ImGui::Image(tex_ref, ImVec2 { 4.f * magnifierSize, 4.f * magnifierSize }, magnifierRectMin / displaySize, magnifierRectMax / displaySize);
            ImGui::Text("Showing: [%.0f, %.0f]x[%.0f, %.0f]", magnifierRectMin.x * displayRatio, magnifierRectMin.y * displayRatio, magnifierRectMax.x * displayRatio, magnifierRectMax.y * displayRatio);

            ImGui::EndTooltip();

            ImGui::GetWindowDrawList()->AddRect(texturePosition + magnifierRectMin, texturePosition + magnifierRectMax, 0xFF0000FF);
        }
    }

    export void HoverableImageCheckerboardBackground(ImTextureRef tex_ref, const ImVec2 &displaySize, float displayRatio, const GuiTextures &guiTextures) {
        const ImVec2 texturePosition = ImGui::GetCursorScreenPos();
        ImageCheckerboardBackground(tex_ref, displaySize, guiTextures);

        if (ImGui::BeginItemTooltip()) {
            float magnifierSize = std::max(displaySize.x, displaySize.y) / 4.f;
            magnifierSize = std::min({ magnifierSize, displaySize.x, displaySize.y });
            const float halfMagnifierSize = magnifierSize / 2.f;

            ImVec2 magnifierRectMin = ImGui::GetIO().MousePos - texturePosition - ImVec2 { halfMagnifierSize, halfMagnifierSize };
            ImVec2 magnifierRectMax = magnifierRectMin + ImVec2 { magnifierSize, magnifierSize };

            if (magnifierRectMin.x < 0.f) {
                magnifierRectMax.x -= magnifierRectMin.x;
                magnifierRectMin.x = 0.f;
            }
            else if (magnifierRectMax.x > displaySize.x) {
                magnifierRectMin.x -= magnifierRectMax.x - displaySize.x;
                magnifierRectMax.x = displaySize.x;
            }

            if (magnifierRectMin.y < 0.f) {
                magnifierRectMax.y -= magnifierRectMin.y;
                magnifierRectMin.y = 0.f;
            }
            else if (magnifierRectMax.y > displaySize.y) {
                magnifierRectMin.y -= magnifierRectMax.y - displaySize.y;
                magnifierRectMax.y = displaySize.y;
            }

            ImageCheckerboardBackground(tex_ref, ImVec2 { 4.f * magnifierSize, 4.f * magnifierSize }, guiTextures, magnifierRectMin / displaySize, magnifierRectMax / displaySize);
            ImGui::Text("Showing: [%.0f, %.0f]x[%.0f, %.0f]", magnifierRectMin.x * displayRatio, magnifierRectMin.y * displayRatio, magnifierRectMax.x * displayRatio, magnifierRectMax.y * displayRatio);

            ImGui::EndTooltip();

            ImGui::GetWindowDrawList()->AddRect(texturePosition + magnifierRectMin, texturePosition + magnifierRectMax, 0xFF0000FF);
        }
    }

    export void WindowWithCenteredText(const char *windowName, std::string_view text) {
        if (ImGui::Begin(windowName)) {
            ImGui::SetCursorPos(ImVec2 { 0.5f, 0.5f } * (ImGui::GetContentRegionAvail() - CalcTextSize(text)) + ImGui::GetStyle().FramePadding);
            TextUnformatted(text);
        }
        ImGui::End();
    }

    export template <typename F>
    struct TableColumnInfo {
        cpp_util::cstring_view label;
        F f;
        ImGuiTableColumnFlags flags;
    };

    template <bool RowNumber, typename... Fs>
    void TableBody(std::size_t rowStart, std::ranges::input_range auto &&items, const Fs &...fs) {
        for (auto &&item : FWD(items)) {
            ImGui::TableNextRow();

            if constexpr (RowNumber) {
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", rowStart);
            }

            INDEX_SEQ(Is, sizeof...(Fs), {
                ((ImGui::TableSetColumnIndex(Is + RowNumber), [&]() {
                    if constexpr (std::invocable<Fs, std::size_t /* row */, decltype(item)>) {
                        fs(rowStart, FWD(item));
                    }
                    else {
                        fs(FWD(item));
                    }
                }()), ...);
            });
            ++rowStart;
        }
    }

    export template <bool RowNumber = true, typename... Fs>
    void Table(cpp_util::cstring_view str_id, ImGuiTableFlags flags, std::ranges::input_range auto &&items, const TableColumnInfo<Fs> &...columnInfos) {
        if (ImGui::BeginTable(str_id.c_str(), RowNumber + sizeof...(Fs), flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            if constexpr (RowNumber) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            }
            (ImGui::TableSetupColumn(columnInfos.label.c_str(), columnInfos.flags), ...);
            ImGui::TableHeadersRow();

            TableBody<RowNumber>(0, FWD(items), columnInfos.f...);

            ImGui::EndTable();
        }
    }

    export template <bool RowNumber = true, typename... Fs>
    void TableWithVirtualization(cpp_util::cstring_view str_id, ImGuiTableFlags flags, std::ranges::random_access_range auto &&items, const TableColumnInfo<Fs> &...columnInfos) {
        // If item count is less than 32, use the normal Table function.
        if (items.size() < 32) {
            Table<RowNumber>(str_id, flags, FWD(items), columnInfos...);
        }
        else if (ImGui::BeginTable(str_id.c_str(), RowNumber + sizeof...(Fs), flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            if constexpr (RowNumber) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            }
            (ImGui::TableSetupColumn(columnInfos.label.c_str(), columnInfos.flags), ...);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin(items.size());
            while (clipper.Step()) {
                auto clippedItems = FWD(items) | std::views::drop(clipper.DisplayStart) | std::views::take(clipper.DisplayEnd - clipper.DisplayStart);
                TableBody<RowNumber>(clipper.DisplayStart, clippedItems, columnInfos.f...);
            }
            ImGui::EndTable();
        }
    }
}