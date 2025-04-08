module;

#include <cassert>

export module vk_gltf_viewer:helpers.imgui;

import std;
export import imgui;
import imgui.internal;
export import :helpers.imgui.table;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace ImGui {
    export bool InputTextWithHint(cpp_util::cstring_view label, cpp_util::cstring_view hint, std::pmr::string* str, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr) {
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
            .ChainCallback = chainCallback,
            .ChainCallbackUserData = userData,
        };
        return InputTextWithHint(label.c_str(), hint.c_str(), str->data(), str->capacity() + 1, flags, chainCallback, &chainedUserData);
    }

    export bool CheckboxTristate(cpp_util::cstring_view label, std::optional<bool> &tristate) {
        bool ret;
        if (tristate) {
            if (bool b = *tristate; (ret = Checkbox(label.c_str(), &b))) {
                *tristate = b;
            }
        }
        else {
            PushItemFlag(ImGuiItemFlags_MixedValue, true);
            if (bool b = false; (ret = Checkbox(label.c_str(), &b))) {
                tristate.emplace(true);
            }
            PopItemFlag();
        }
        return ret;
    }

    // https://github.com/ocornut/imgui/pull/6526
    export bool SmallCheckbox(cpp_util::cstring_view label, bool* v) {
        ImGuiStyle &style = GetStyle();
        const float backup_padding_y = style.FramePadding.y;
        style.FramePadding.y = 0.0f;
        bool pressed = Checkbox(label.c_str(), v);
        style.FramePadding.y = backup_padding_y;
        return pressed;
    }

    export void TextUnformatted(std::string_view str) {
        Text(str.data(), str.data() + str.size());
    }

    export void HelperMarker(cpp_util::cstring_view label, std::string_view description) {
        TextDisabled("%s", label.c_str());
        if (BeginItemTooltip()) {
            TextUnformatted(description);
            EndTooltip();
        }
    }

    export bool ImageButtonWithText(std::string_view str_id, ImTextureID user_texture_id, std::string_view text, const ImVec2 &image_size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& bg_col = ImVec4(0, 0, 0, 0), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), ImGuiButtonFlags flags = 0) {
        ImGuiStyle &style = GetStyle();
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const ImGuiID id = window->GetID(str_id.data(), str_id.data() + str_id.size());

        const ImVec2 padding = style.FramePadding;
        const ImRect bb(window->DC.CursorPos, { window->DC.CursorPos.x + image_size.x + padding.x * 2.0f, window->DC.CursorPos.y + image_size.y + padding.y * 2.0f + GetTextLineHeight() });
        ItemSize(bb);
        if (!ItemAdd(bb, id))
            return false;

        bool hovered, held;
        bool pressed = ButtonBehavior(bb, id, &hovered, &held, flags);

        // Render
        const ImU32 col = GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        RenderNavCursor(bb, id);
        RenderFrame(bb.Min, bb.Max, col, true, std::clamp(std::min(padding.x, padding.y), 0.0f, style.FrameRounding));

        const ImVec2 p_min { bb.Min.x + padding.x, bb.Min.y + padding.y };
        const ImVec2 p_max { bb.Max.x - padding.x, bb.Max.y - padding.y - GetTextLineHeight() };
        if (bg_col.w > 0.0f)
            window->DrawList->AddRectFilled(p_min, p_max, GetColorU32(bg_col));
        window->DrawList->AddImage(user_texture_id, p_min, p_max, uv0, uv1, GetColorU32(tint_col));
        window->DrawList->PushClipRect({ p_min.x, p_max.y }, { p_max.x, p_max.y + GetTextLineHeight() }, true);
        window->DrawList->AddText({ p_min.x, p_max.y }, GetColorU32(ImGuiCol_Text), text.data(), text.data() + text.size());
        window->DrawList->PopClipRect();

        return pressed;
    }

    export template <std::invocable F>
    void WithLabel(std::string_view label, F &&imGuiFunc)
        requires std::is_void_v<std::invoke_result_t<F>>
    {
        const float x = GetCursorPosX();
        std::invoke(FWD(imGuiFunc));
        SameLine();
        SetCursorPosX(x + CalcItemWidth() + GetStyle().ItemInnerSpacing.x);
        TextUnformatted(label);
    }

    export template <std::invocable F>
    std::invoke_result_t<F> WithLabel(std::string_view label, F &&imGuiFunc) {
        const float x = GetCursorPosX();
        auto value = std::invoke(FWD(imGuiFunc));
        SameLine();
        SetCursorPosX(x + CalcItemWidth() + GetStyle().ItemInnerSpacing.x);
        TextUnformatted(label);
        return value;
    }

    export template <std::invocable F>
    void WithGroup(F &&f, bool flag = true) {
        if (flag) BeginGroup();
        std::invoke(FWD(f));
        if (flag) EndGroup();
    }

    export template <std::invocable F>
    void WithDisabled(F &&f, bool flag = true) {
        BeginDisabled(flag);
        std::invoke(FWD(f));
        EndDisabled();
    }

    export template <std::invocable F>
    void WithID(std::string_view id, F &&f) {
        PushID(id.data(), id.data() + id.size());
        std::invoke(FWD(f));
        PopID();
    }

    export template <std::invocable F>
    void WithID(auto id, F &&f) {
        PushID(id);
        std::invoke(FWD(f));
        PopID();
    }

    export template <std::invocable F>
    void WithItemWidth(float width, F &&f) {
        PushItemWidth(width);
        std::invoke(FWD(f));
        PopItemWidth();
    }

    export template <std::invocable F>
    void WithStyleColor(int index, const ImVec4 &color, F &&f, bool flag = true)
        requires std::is_void_v<std::invoke_result_t<F>>
    {
        if (flag) PushStyleColor(index, color);
        std::invoke(FWD(f));
        if (flag) PopStyleColor();
    }

    export template <std::invocable F>
    [[nodiscard]] std::invoke_result_t<F> WithStyleColor(int index, const ImVec4 &color, F &&f, bool flag = true) {
        if (flag) PushStyleColor(index, color);
        auto result = std::invoke(FWD(f));
        if (flag) PopStyleColor();
        return result;
    }
}