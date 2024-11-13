export module vk_gltf_viewer:helpers.formatters.joiner;

import std;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

template <std::size_t N>
struct static_string {
    char __str_[N];

    constexpr static_string(const char (&str)[N]) noexcept {
        std::copy_n(str, N, __str_);
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept {
        return { __str_, N - 1 /* last character would be '\0' */ };
    }
};

template <std::ranges::range R, static_string /* Delimiter */>
struct joiner {
    std::ranges::ref_view<R> r;
};

export template <static_string Delimiter>
auto make_joiner(std::ranges::range auto const &r) -> joiner<std::remove_reference_t<decltype(r)>, Delimiter> {
    return { r };
}

export template <std::ranges::range R, static_string Delimiter>
struct std::formatter<joiner<R, Delimiter>> : range_formatter<ranges::range_value_t<R>> {
    constexpr formatter() {
        range_formatter<ranges::range_value_t<R>>::set_brackets("", "");
        range_formatter<ranges::range_value_t<R>>::set_separator(Delimiter);
    }

    constexpr auto format(joiner<R, Delimiter> x, auto& ctx) const {
        return range_formatter<ranges::range_value_t<R>>::format(x.r, ctx);
    }
};