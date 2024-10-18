module;

#include <version>

export module vk_gltf_viewer:helpers.ranges;
export import :helpers.ranges.concat;
export import :helpers.ranges.contains;

import std;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

// This macro is for Clang's false-positive build error when using lambda in initializer of non-inline variable in named
// modules. We can specify the inline keyword to workaround this.
// See: https://github.com/llvm/llvm-project/issues/110146
// TODO: remove this macro when the issue fixed.
#if __clang__
#define CLANG_INLINE inline
#else
#define CLANG_INLINE
#endif

namespace ranges {
    export template <typename Derived>
#if __cpp_lib_ranges >= 202202L && (!defined(_LIBCPP_VERSION) || _LIBCPP_VERSION >= 190100) // https://github.com/llvm/llvm-project/issues/70557#issuecomment-1851936055
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

    /**
     * @brief Make range from iterator and sentinel pair.
     * @tparam I Iterator type.
     * @tparam S Sentinel type.
     * @param pair A pair of iterator and sentinel.
     * @return A range from iterator to sentinel.
     */
    export template <std::input_or_output_iterator I, std::sentinel_for<I> S>
    [[nodiscard]] auto make_subrange(std::pair<I, S> pair) {
        return std::ranges::subrange(pair.first, pair.second);
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

    export CLANG_INLINE constexpr auto deref = std::views::transform([](auto &&x) -> decltype(auto) {
        return *FWD(x);
    });

    /**
     * A range adaptor object that apply a transformation function, invocable with underlying sequence's tuple-like elements.
     * @example
     * @code
     * using Person = std::pair<std::string, int>; // name, age.
     * std::vector people { Person { "Alice", 20 }, Person { "Bob", 30 } };
     * auto stringified = people | ranges::views::decompose_transform([](std::string_view name, int age) {
     *     return std::format("Person(name={:?}, age={})", name, age);
     * });
     * std::println("{::s}", stringified); // Output: [Person(name="Alice", age=20), Person(name="Bob", age=30)]
     * @endcode
     */
    export constexpr struct decompose_transform_fn {
        [[nodiscard]] auto operator()(auto &&f) const {
            return std::views::transform([f = FWD(f)](auto &&xs) {
                return std::apply(f, FWD(xs));
            });
        }

        [[nodiscard]] auto operator()(std::ranges::viewable_range auto &&r, auto &&f) const {
            return FWD(r) | this->operator()(FWD(f));
        }
    } decompose_transform;

    /**
     * A range adaptor object that transform the value underlying sequence's pair-like elements.
     * @example
     * @code
     * std::unordered_map<std::string, int> people { { "Alice", 20 }, { "Bob", 30 } };
     * std::unordered_map after_10_years { std::from_range, people | ranges::views::value_transform([](int age) { return age + 10; }) };
     * // after_10_years == { { "Alice", 30 }, { "Bob", 40 } }
     * @endcode
     */
    export constexpr struct value_transform_fn {
        [[nodiscard]] auto operator()(auto &&f) const {
            return std::views::transform([f = FWD(f)](auto &&pair) {
                auto &&[key, value] = FWD(pair);
                return std::pair { FWD(key), f(FWD(value)) };
            });
        }

        [[nodiscard]] auto operator()(std::ranges::viewable_range auto &&r, auto &&f) const {
            return FWD(r) | this->operator()(FWD(f));
        }
    } value_transform;
}
}