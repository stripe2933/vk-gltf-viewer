module;

#include <version>

export module ranges;

import std;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace ranges {
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

    export template <std::size_t N>
    struct to_array : range_adaptor_closure<to_array<N>> {
        template <std::ranges::input_range R>
        [[nodiscard]] constexpr auto operator()(R &&r) const -> std::array<std::ranges::range_value_t<R>, N> {
            auto it = r.begin();
            return ARRAY_OF(N, *it++);
        }
    };

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

    /**
     * Get value from associative container, or return default value if not found.
     * @tparam AssociativeContainer
     * @tparam Key
     * @tparam Value
     * @param c an associative container.
     * @param key key to find.
     * @param default_value a value to return if mapping not exist.
     * @return Value mapped to key, or default_value if not found.
     * @note This function is intended to be usable with immutable container (not calling subscript operator).
     */
    export template <
        typename AssociativeContainer,
        typename Key = std::remove_cvref_t<AssociativeContainer>::key_type,
        typename T = std::remove_cvref_t<AssociativeContainer>::mapped_type>
    [[nodiscard]] constexpr auto value_or(AssociativeContainer &&c, const Key &key, T default_value) noexcept -> T {
        const auto it = c.find(key);
        return it == c.end() ? default_value : it->second;
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

#if __cpp_lib_ranges_zip >= 202110L
    export template <std::size_t N>
    constexpr decltype(std::views::adjacent<N>) adjacent;
    export constexpr decltype(std::views::pairwise) pairwise;
#else
    template <std::size_t N>
    struct adjacent_fn : range_adaptor_closure<adjacent_fn<N>> {
        [[nodiscard]] static constexpr auto operator()(std::ranges::forward_range auto &&r) {
            return INDEX_SEQ(Is, N, {
                return std::views::zip(r | std::views::drop(Is)...);
            });
        }
    };
    export template <std::size_t N>
    constexpr adjacent_fn<N> adjacent;
    export constexpr adjacent_fn<2> pairwise;
#endif

#if __cpp_lib_ranges_zip >= 202110L
    export constexpr decltype(std::views::zip_transform) zip_transform;
#else
    export
    [[nodiscard]] constexpr auto zip_transform(auto &&f, std::ranges::input_range auto &&...rs) -> auto {
        return std::views::zip(FWD(rs)...) | std::views::transform([&](auto &&t) {
            return std::apply(f, FWD(t));
        });
    }
#endif

    struct deref_fn : range_adaptor_closure<deref_fn> {
        [[nodiscard]] constexpr auto operator()(std::ranges::input_range auto &&r) const {
            return FWD(r) | std::views::transform([](auto &&x) -> decltype(auto) {
                return *FWD(x);
            });
        }
    };
    export constexpr deref_fn deref;

    export constexpr struct decompose_transform_fn {
        [[nodiscard]] auto operator()(std::ranges::viewable_range auto &&r, auto &&f) const {
            return std::views::transform(FWD(r), [f = FWD(f)](auto &&xs) {
                return std::apply(f, FWD(xs));
            });
        }

        [[nodiscard]] auto operator()(auto &&f) const {
            return std::views::transform([f = FWD(f)](auto &&xs) {
                return std::apply(f, FWD(xs));
            });
        }
    } decompose_transform;
}
}