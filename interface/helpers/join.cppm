export module vk_gltf_viewer.helpers;

import std;

export template <char, std::ranges::range R>
struct joiner {
    R r;
};

export template <char Delimiter, std::ranges::input_range R, typename CharT>
struct std::formatter<joiner<Delimiter, R>, CharT> : range_formatter<std::ranges::range_value_t<R>, CharT> {
    static constexpr char delimiter = Delimiter;

    constexpr formatter() {
        range_formatter<std::ranges::range_value_t<R>, CharT>::set_separator(std::string_view { &delimiter, 1 });
    }

    [[nodiscard]] constexpr auto format(joiner<Delimiter, R> joiner, auto &ctx) const {
        return range_formatter<std::ranges::range_value_t<R>, CharT>::format(joiner.r, ctx);
    }
};

/**
 * Make a lightweight container that stores the given \p r as reference and can be formatted by the given \tp Delimiter.
 * @tparam Delimiter Delimiter character.
 * @tparam R Range type.
 * @param r Range to be formatted.
 * @return A struct that has custom formatting rule.
 */
export template <char Delimiter, std::ranges::input_range R>
[[nodiscard]] constexpr joiner<Delimiter, std::ranges::ref_view<const R>> join(const R &r) noexcept {
    return { std::ranges::ref_view { r } };
}