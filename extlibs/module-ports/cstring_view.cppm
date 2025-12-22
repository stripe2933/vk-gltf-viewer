module;

#include <version>

#if __cpp_lib_modules < 202207L
#include <cstring_view.hpp>
#endif

export module cstring_view;

#if __cpp_lib_modules >= 202207L
import std;

#define CPP_UTIL_CSTRING_VIEW_USE_STD_MODULE

extern "C++" {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 5244) // Including header in the purview of module 'cstring_view' appears erroneous.
#endif

#include <cstring_view.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
}
#endif

export namespace cpp_util {
    using cpp_util::basic_cstring_view;
    using cpp_util::swap;
    using cpp_util::cstring_view;
#if defined(__cpp_char8_t)
    using cpp_util::u8cstring_view;
#endif
    using cpp_util::u16cstring_view;
    using cpp_util::u32cstring_view;
    using cpp_util::wcstring_view;

    // Make formattable by fmt
    template <typename CharT, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] constexpr std::basic_string_view<CharT, Traits> format_as(basic_cstring_view<CharT, Traits> csv) noexcept {
        return csv;
    }

inline namespace literals {
inline namespace string_view_literals {
    using string_view_literals::operator""_csv;
} // namespace string_view_literals
} // namespace literals
} // namespace cpp_util

export namespace std {
    using std::hash;

#if defined(__cpp_lib_ranges)
namespace ranges {
    using ranges::enable_borrowed_range;
    using ranges::enable_view;
} // namespace ranges
#endif

#if defined(__cpp_lib_format)
    using std::formatter;
#elif _LIBCPP_VERSION >= 17 // libc++ support text formatting since version 17, but the feature test macro has not been defined until 19.
    template <typename CharT, typename Traits>
    struct formatter<cpp_util::basic_cstring_view<CharT, Traits>> : formatter<basic_string_view<CharT, Traits>> {
        auto format(cpp_util::basic_cstring_view<CharT, Traits> csv, auto& ctx) const {
            return formatter<basic_string_view<CharT, Traits>>::format(static_cast<basic_string_view<CharT, Traits>>(csv), ctx);
        }
    };
#endif

// #if __cpp_lib_format_ranges >= 202207L
//     template <typename CharT, typename Traits>
//     constexpr range_format format_kind<cpp_util::basic_cstring_view<CharT, Traits>> = range_format::disabled;
// #endif
} // namespace std