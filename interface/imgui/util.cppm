export module vk_gltf_viewer.imgui:util;

import std;
export import imgui;

namespace vk_gltf_viewer::imgui {
    export ImVec2 CalcTextSize(std::string_view text, bool hide_text_after_double_hash = false, float wrapWidth = -1) {
        return ImGui::CalcTextSize(text.data(), text.data() + text.size(), hide_text_after_double_hash, wrapWidth);
    }

    /**
     * @brief Return text that truncates the beginning with ellipsis to fit within the given width.
     *
     * It uses binary search to find the truncation point. Complexity: O(text.size() + log(text.size()) * ellipsis.size()).
     * No memory allocation is performed and the operation is done within \p text.
     *
     * @param text Text to be truncated. Size must be at least \p ellipsis.size().
     * @param width Maximum width.
     * @param ellipsis Ellipsis string. Default is "...".
     * @return Portion of \p text that represents the truncated text. This does not contain \p ellipsis, i.e. result ⊂ \p text.
     */
    export std::span<char> EllipsisLeft(std::span<char> text, float width, std::string ellipsis = "...") {
		const std::span ellipsisPrefixRemovedText = text.subspan(ellipsis.size());
        const auto it = std::ranges::upper_bound(
            ellipsisPrefixRemovedText,
            width, std::ranges::greater{},
            [&](char &c) {
                // Retrieve the index of current character.
                const std::size_t i = &c - text.data();

                // Replace the characters right before the current character with ellipsis temporarily, and calculate
                // the text size including the ellipsis.
                std::swap_ranges(ellipsis.begin(), ellipsis.end(), &text[i - ellipsis.size()]);
                const float result = CalcTextSize(std::string_view { text }.substr(i - ellipsis.size())).x;

                // Restore the original characters.
                std::swap_ranges(ellipsis.begin(), ellipsis.end(), &text[i - ellipsis.size()]);
                return result;
            });
        return text.subspan(ellipsis.size() + std::ranges::distance(ellipsisPrefixRemovedText.begin(), it));
    }

}