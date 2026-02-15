export module vk_gltf_viewer.imgui:scoped;

import std;
export import imgui;

import :widget;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vk_gltf_viewer::imgui {
    export struct GroupScoped {
        explicit GroupScoped() {
            ImGui::BeginGroup();
        }

        ~GroupScoped() {
            ImGui::EndGroup();
        }
    };

    export struct DisabledScoped {
        explicit DisabledScoped() {
            ImGui::BeginDisabled();
        }

        ~DisabledScoped() {
            ImGui::EndDisabled();
        }
    };

    export struct IDScoped {
        explicit IDScoped(std::string_view id) {
            ImGui::PushID(id.data(), id.data() + id.size());
        }

        explicit IDScoped(const void *id) {
            ImGui::PushID(id);
        }

        explicit IDScoped(int id) {
            ImGui::PushID(id);
        }

        ~IDScoped() {
            ImGui::PopID();
        }
    };

    export struct ItemWidthScoped {
        explicit ItemWidthScoped(float width) {
            ImGui::PushItemWidth(width);
        }

        ~ItemWidthScoped() {
            ImGui::PopItemWidth();
        }
    };

    export struct StyleColorScoped {
        StyleColorScoped(int index, const ImVec4 &color) {
            ImGui::PushStyleColor(index, color);
        }

        StyleColorScoped(int index, ImU32 color) {
            ImGui::PushStyleColor(index, color);
        }

        ~StyleColorScoped() {
            ImGui::PopStyleColor();
        }
    };

    export void WithLabel(std::string_view label, std::invocable auto &&imGuiFunc) {
        const float x = ImGui::GetCursorPosX();
        std::invoke(FWD(imGuiFunc));
        ImGui::SameLine();
        ImGui::SetCursorPosX(x + ImGui::CalcItemWidth() + ImGui::GetStyle().ItemInnerSpacing.x);
        widget::TextUnformatted(label);
    }

    export void WithGroup(std::invocable auto &&f, bool flag = true) {
        std::optional<GroupScoped> scope;
        if (flag) scope.emplace();

        std::invoke(FWD(f));
    }

    export void WithDisabled(std::invocable auto &&f, bool flag = true) {
        std::optional<DisabledScoped> scope;
        if (flag) scope.emplace();

        std::invoke(FWD(f));
    }

    export void WithID(std::string_view id, std::invocable auto &&f) {
        IDScoped _ { id };
        std::invoke(FWD(f));
    }

    export void WithID(const void *id, std::invocable auto &&f) {
        IDScoped _ { id };
        std::invoke(FWD(f));
    }

    export void WithID(int id, std::invocable auto &&f) {
        IDScoped _ { id };
        std::invoke(FWD(f));
    }

    export void WithItemWidth(float width, std::invocable auto &&f) {
        ItemWidthScoped _ { width };
        std::invoke(FWD(f));
    }

    export void WithStyleColor(int index, const ImVec4 &color, std::invocable auto &&f, bool flag = true) {
        std::optional<StyleColorScoped> scope;
        if (flag) scope.emplace(index, color);

        std::invoke(FWD(f));
    }

    export void WithStyleColor(int index, ImU32 color, std::invocable auto &&f, bool flag = true) {
        std::optional<StyleColorScoped> scope;
        if (flag) scope.emplace(index, color);

        std::invoke(FWD(f));
    }
}