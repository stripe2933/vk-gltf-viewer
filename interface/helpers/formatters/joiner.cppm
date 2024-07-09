module;

#include <version>

export module vk_gltf_viewer:helpers.formatters.joiner;

import std;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vk_gltf_viewer {
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
}

// MSVC have some lack of supports for exporting template specialization in C++20 module.
// https://developercommunity.visualstudio.com/t/C-module-failed-to-export-the-speciali/10396010?q=DEP+0700+Registration+of+the+App+Failed
// A simple solution for this: wrap them with std namespace.
namespace std {
#if __cpp_lib_format_ranges >= 202207L
    export template <ranges::range R, vk_gltf_viewer::static_string Delimiter>
        struct formatter<vk_gltf_viewer::joiner<R, Delimiter>> : range_formatter<ranges::range_value_t<R>> {
        constexpr formatter() {
            range_formatter<ranges::range_value_t<R>>::set_brackets("", "");
            range_formatter<ranges::range_value_t<R>>::set_separator(Delimiter);
        }

        constexpr auto format(vk_gltf_viewer::joiner<R, Delimiter> x, auto& ctx) const {
            return range_formatter<ranges::range_value_t<R>>::format(x.r, ctx);
        }
    };
#else
    export template <ranges::range R, vk_gltf_viewer::static_string Delimiter>
        struct formatter<vk_gltf_viewer::joiner<R, Delimiter>, char> : formatter<ranges::range_value_t<R>, char> {
        constexpr auto format(vk_gltf_viewer::joiner<R, Delimiter> x, auto& ctx) const {
            formatter<ranges::range_value_t<R>, char>::format(x.r.front(), ctx);
            for (auto&& e : x.r | views::drop(1)) {
                format_to(ctx.out(), "{}", static_cast<string_view>(Delimiter));
                formatter<ranges::range_value_t<R>, char>::format(e, ctx);
            }
            return ctx.out();
        }
    };
#endif
}