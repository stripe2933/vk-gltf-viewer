module;

#include <concepts>
#include <ranges>
#include <type_traits>
#include <version>

export module pbrenvmap:details.ranges;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace pbrenvmap::inline details::ranges {
    export template <typename Derived>
#if !defined(_LIBCPP_VERSION) && __cpp_lib_ranges >= 202202L // https://github.com/llvm/llvm-project/issues/70557#issuecomment-1851936055
    using range_adaptor_closure = std::ranges::range_adaptor_closure<Derived>;
#else
        requires std::is_object_v<Derived> && std::same_as<Derived, std::remove_cv_t<Derived>>
    struct range_adaptor_closure {
        template <std::ranges::range R>
        [[nodiscard]] friend constexpr auto operator|(R &&r, const Derived &derived) noexcept(std::is_nothrow_invocable_v<const Derived&, R>) {
            return derived(FWD(r));
        }
    };
#endif

    /**
     * Concatenate variadic arrays into one array.
     * @tparam ArrayT Array types, must have same value_type.
     * @param arrays Arrays to be concatenated.
     * @return Concatenated array.
     * @code
     * array_cat(std::array { 1, 2 }, std::array { 3, 4, 5 }) == std::array { 1, 2, 3, 4, 5 };
     * @endcode
     */
    export template <typename... ArrayT>
    [[nodiscard]] constexpr auto array_cat(ArrayT &&...arrays) -> auto {
        return std::apply([](auto &&...xs) {
            return std::array { FWD(xs)... };
        }, tuple_cat(FWD(arrays)...));
    }

namespace views {
#if __cpp_lib_ranges_enumerate >= 202302L
    export constexpr decltype(std::views::enumerate) enumerate;
#else
    struct enumerate_fn : range_adaptor_closure<enumerate_fn> {
        template <std::ranges::viewable_range R>
        [[nodiscard]] static constexpr auto operator()(R &&r) -> auto {
            if constexpr (std::ranges::sized_range<R>) {
                return std::views::zip(
                    std::views::iota(static_cast<std::ranges::range_difference_t<R>>(0), static_cast<std::ranges::range_difference_t<R>>(r.size())),
                    FWD(r));
            }
            else {
                return std::views::zip(
                    std::views::iota(static_cast<std::ranges::range_difference_t<R>>(0)),
                    FWD(r));
            }
        }
    };

    export constexpr enumerate_fn enumerate;
#endif

    struct deref_fn : range_adaptor_closure<deref_fn> {
        [[nodiscard]] constexpr auto operator()(std::ranges::input_range auto &&r) const {
            return FWD(r) | std::views::transform([](auto &&x) -> decltype(auto) {
                return *FWD(x);
            });
        }
    };
    export constexpr deref_fn deref;
}
}