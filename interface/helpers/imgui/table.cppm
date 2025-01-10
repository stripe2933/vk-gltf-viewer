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

    template <bool RowNumber, typename... Fs>
    void TableBody(std::size_t rowStart, std::ranges::input_range auto &&items, const Fs &...fs) {
        for (auto &&item : FWD(items)) {
            TableNextRow();

            if constexpr (RowNumber) {
                TableSetColumnIndex(0);
                Text("%zu", rowStart);
            }

            INDEX_SEQ(Is, sizeof...(Fs), {
                ((TableSetColumnIndex(Is + RowNumber), [&]() {
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
    void Table(cpp_util::cstring_view str_id, ImGuiTableFlags flags, std::ranges::input_range auto &&items, const ColumnInfo<Fs> &...columnInfos) {
        if (BeginTable(str_id.c_str(), RowNumber + sizeof...(Fs), flags)) {
            TableSetupScrollFreeze(0, 1);
            if constexpr (RowNumber) {
                TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            }
            (TableSetupColumn(columnInfos.label.c_str(), columnInfos.flags), ...);
            TableHeadersRow();

            TableBody<RowNumber>(0, FWD(items), columnInfos.f...);

            EndTable();
        }
    }

    export template <bool RowNumber = true, typename... Fs>
    void TableWithVirtualization(cpp_util::cstring_view str_id, ImGuiTableFlags flags, std::ranges::random_access_range auto &&items, const ColumnInfo<Fs> &...columnInfos) {
        // If item count is less than 32, use the normal Table function.
        if (items.size() < 32) {
            Table<RowNumber>(str_id, flags, FWD(items), columnInfos...);
        }
        else if (BeginTable(str_id.c_str(), RowNumber + sizeof...(Fs), flags)) {
            TableSetupScrollFreeze(0, 1);
            if constexpr (RowNumber) {
                TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            }
            (TableSetupColumn(columnInfos.label.c_str(), columnInfos.flags), ...);
            TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin(items.size());
            while (clipper.Step()) {
                auto clippedItems = FWD(items) | std::views::drop(clipper.DisplayStart) | std::views::take(clipper.DisplayEnd - clipper.DisplayStart);
                TableBody<RowNumber>(clipper.DisplayStart, clippedItems, columnInfos.f...);
            }
            EndTable();
        }
    }
}