export module vk_gltf_viewer:helpers.imgui.table;

import std;
export import cstring_view;
import imgui;

#define INDEX_SEQ(Is, N, ...) [&]<auto... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace ImGui {
    export template <typename F>
    struct ColumnInfo {
        using function_t = F;

        cpp_util::cstring_view label;
        F f;
        ImGuiTableColumnFlags flags;
    };

    export template <typename... Fs>
    auto Table(cpp_util::cstring_view str_id, ImGuiTableFlags flags, std::ranges::input_range auto &&items, const ColumnInfo<Fs> &...columnInfos) -> void {
        if (BeginTable(str_id.c_str(), 1 /* row index */ + sizeof...(Fs), flags)) {
            TableSetupScrollFreeze(0, 1);
            TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            (TableSetupColumn(columnInfos.label.c_str(), columnInfos.flags), ...);
            TableHeadersRow();
            for (std::size_t rowIndex = 0; auto &&item : FWD(items)) {
                TableNextRow();
                TableSetColumnIndex(0);
                Text("%zu", rowIndex);
                INDEX_SEQ(Is, sizeof...(Fs), {
                    ((TableSetColumnIndex(Is + 1), [&]() {
                        if constexpr (std::invocable<Fs, std::size_t /* row */, decltype(item)>) {
                            columnInfos.f(rowIndex, FWD(item));
                        }
                        else {
                            columnInfos.f(FWD(item));
                        }
                    }()), ...);
                });
                ++rowIndex;
            }
            EndTable();
        }
    }

    export template <typename... Fs>
    auto TableNoRowNumber(cpp_util::cstring_view str_id, ImGuiTableFlags flags, std::ranges::input_range auto &&items, const ColumnInfo<Fs> &...columnInfos) -> void {
        if (BeginTable(str_id.c_str(), sizeof...(Fs), flags)) {
            TableSetupScrollFreeze(0, 1);
            (TableSetupColumn(columnInfos.label.c_str(), columnInfos.flags), ...);
            TableHeadersRow();
            for (std::size_t rowIndex = 0; auto &&item : FWD(items)) {
                TableNextRow();
                INDEX_SEQ(Is, sizeof...(Fs), {
                    ((TableSetColumnIndex(Is), [&]() {
                        if constexpr (std::invocable<Fs, std::size_t /* row */, decltype(item)>) {
                            columnInfos.f(rowIndex, FWD(item));
                        }
                        else {
                            columnInfos.f(FWD(item));
                        }
                    }()), ...);
                });
                ++rowIndex;
            }
            EndTable();
        }
    }
}