module;

#include <cassert>
#include <version>

export module vk_gltf_viewer.helpers.ranges;
export import :concat;
export import :contains;

import std;
import vk_gltf_viewer.helpers.concepts;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define NOEXCEPT_IF(...) noexcept(noexcept(__VA_ARGS__))

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
    [[nodiscard]] constexpr T value_or(AssociativeContainer &&c, const Key &key, T default_value) noexcept {
        const auto it = c.find(key);
        return it == c.end() ? default_value : it->second;
    }

    /**
     * @brief If a key equivalent to \p key already exists in the container, return an iterator to it. Otherwise, generate the value by invoking \p f, insert it into the container, and return the iterator to the new element.
     * @tparam AssociativeContainer An associative container type.
     * @tparam Key Key type. This have to be as same as \p AssociativeContainer::key_type, unless transparent hashing is enabled.
     * @param c An associative container.
     * @param key Key to find.
     * @param f A function to generate the value if key is not exists. If key already exists, this function will not be called.
     * @return A pair consisting of an iterator to the element and a bool denoting whether the element was inserted.
     */
    export template <
        typename AssociativeContainer,
        typename Key = AssociativeContainer::key_type>
    [[nodiscard]] constexpr std::pair<typename AssociativeContainer::iterator, bool> try_emplace_if_not_exists(
        AssociativeContainer &c,
        const Key &key,
        concepts::signature_of<typename AssociativeContainer::mapped_type()> auto const &f
    ) {
        if (auto it = c.find(key); it != c.end()) {
            return { it, false };
        }
        else {
            return { c.try_emplace(it, key, f()), true };
        }
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

    export template <std::equality_comparable T, std::ranges::input_range R = std::initializer_list<T>>
    [[nodiscard]] constexpr bool one_of(const T &value, const R &candidates) NOEXCEPT_IF(std::declval<T>() == std::declval<T>()) {
        return contains(candidates, value);
    }

namespace views {
    export template <std::integral T>
    [[nodiscard]] constexpr auto upto(T n) noexcept {
        assert(n >= 0 && "n must be non-negative.");
        return std::views::iota(T { 0 }, n);
    }

#if __cpp_lib_ranges_enumerate >= 202302L
    export constexpr decltype(std::views::enumerate) enumerate;
#else
    struct enumerate_fn : range_adaptor_closure<enumerate_fn> {
        template <std::ranges::viewable_range R>
        [[nodiscard]] static constexpr auto operator()(R &&r) {
            if constexpr (std::ranges::sized_range<R>) {
                return std::views::zip(upto<std::ranges::range_difference_t<R>>(r.size()), FWD(r));
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
    [[nodiscard]] constexpr auto zip_transform(auto &&f, std::ranges::input_range auto &&...rs) {
        return std::views::zip(FWD(rs)...) | std::views::transform([&](auto &&t) {
            return std::apply(f, FWD(t));
        });
    }
#endif

    export CLANG_INLINE constexpr auto deref = std::views::transform([](auto &&x) -> decltype(auto) {
        return *FWD(x);
    });

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

    export template <std::invocable G>
    [[nodiscard]] constexpr auto generate_n(std::size_t n, G &&gen) noexcept(std::is_nothrow_move_constructible_v<G>) {
        return upto(n) | std::views::transform([gen = FWD(gen)](auto) noexcept(std::is_nothrow_invocable_v<G>) {
            return std::invoke(gen);
        });
    }
}
}
