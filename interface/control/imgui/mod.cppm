module;

#include <imgui.h>
#include <imgui_internal.h>

export module imgui;

import std;
export import :table;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace ImGui {
    export bool InputTextWithHint(cstring_view label, cstring_view hint, std::pmr::string* str, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr) {
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
        return InputTextWithHint(label.c_str(), hint.c_str(), str->data(), str->capacity() + 1, flags, chainCallback, &chainedUserData);
    }

    export bool CheckboxTristate(cstring_view label, std::optional<bool> &tristate) {
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
    export bool SmallCheckbox(cstring_view label, bool* v) {
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

    export template <std::invocable F>
    auto WithLabel(std::string_view label, F &&imGuiFunc) -> void
        requires std::is_void_v<std::invoke_result_t<F>>
    {
        const float x = GetCursorPosX();
        std::invoke(FWD(imGuiFunc));
        SameLine();
        SetCursorPosX(x + CalcItemWidth() + GetStyle().ItemInnerSpacing.x);
        TextUnformatted(label);
    }

    export template <std::invocable F>
    auto WithLabel(std::string_view label, F &&imGuiFunc) -> std::invoke_result_t<F> {
        const float x = GetCursorPosX();
        auto value = std::invoke(FWD(imGuiFunc));
        SameLine();
        SetCursorPosX(x + CalcItemWidth() + GetStyle().ItemInnerSpacing.x);
        TextUnformatted(label);
        return value;
    }
}