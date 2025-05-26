export module vk_gltf_viewer.helpers.full_variant;

import std;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define INDEX_SEQ(Is, N, ...) [&]<auto ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})

template <typename T, typename... Ts>
concept one_of = (std::same_as<T, Ts> || ...);

template <typename T, typename... Ts>
constexpr bool all_same = (std::same_as<T, Ts> && ...);

template <typename T, typename TFirst, typename... TTail>
[[nodiscard]] consteval std::uint8_t type_index() {
    if constexpr (std::same_as<T, TFirst>) return 0;
    else return 1 + type_index<T, TTail...>();
}

export template <typename... Ts>
    requires (sizeof...(Ts) < 256)
class full_variant {
    std::tuple<Ts...> values;
    std::uint8_t active;

public:
    constexpr full_variant(std::uint8_t active = 0) noexcept((std::is_nothrow_constructible_v<Ts> && ...))
        : active { active } { }

    template <std::convertible_to<Ts>... Us> requires (sizeof...(Us) == sizeof...(Ts))
    constexpr full_variant(std::uint8_t active, Us &&...args) noexcept((std::is_nothrow_constructible_v<Ts, Us> && ...))
        : values { FWD(args)... }
        , active { active } { }

    constexpr full_variant(const full_variant&) noexcept((std::is_nothrow_constructible_v<Ts> && ...)) = default;
    constexpr full_variant(full_variant&&) noexcept = default;

    [[nodiscard]] constexpr std::uint8_t index() const noexcept { return active; }
    constexpr void set_index(std::uint8_t index) noexcept { active = index; }

    template <one_of<Ts...> T>
    constexpr void set_alternative() noexcept {
        active = type_index<T, Ts...>();
    }

    template <one_of<Ts...> T>
    [[nodiscard]] constexpr bool holds_alternative() const noexcept {
        return active == type_index<T, Ts...>();
    }

    /**
     * @brief Get alternative at the index \tp I.
     * @tparam I Index of the alternative to get.
     * @return Reference to the alternative at the index \p I.
     */
    template <std::uint8_t I>
    [[nodiscard]] constexpr decltype(auto) raw(this auto &&self) noexcept {
        return get<I>(FWD(self).values);
    }

    /**
     * @brief Applies the visitor \p vis (a Callable that can be called with any combination of types from the variant)
     * to the variant held by \p self.
     * @param self Variant to pass the visitor.
     * @param vis A callable that accepts every possible alternative from the variant.
     */
    template <typename Self, typename Visitor>
        requires (all_same<void, std::invoke_result_t<Visitor, decltype(std::forward_like<Self>(std::declval<Ts>()))>...>)
    void visit(this Self &&self, Visitor &&vis) {
        INDEX_SEQ(Is, sizeof...(Ts), {
            std::ignore = ([&]() {
                if (self.active == Is) {
                    std::invoke(FWD(vis), FWD(self).template raw<Is>());
                    return true;
                }
                else {
                    return false;
                }
            }() || ...);
        });
    }

    /**
     * @copydoc visit(this Self &&self, Visitor &&vis)
     */
    template <typename Self, typename Visitor>
        requires (sizeof...(Ts) <= 8) // I have no idea how to implement this function with arbitrarily many alternatives.
            && (!all_same<void, std::invoke_result_t<Visitor, decltype(std::forward_like<Self>(std::declval<Ts>()))>...>)
    [[nodiscard]] decltype(auto) visit(this Self &&self, Visitor &&vis) {
        #define CASE(Is) if (self.active == Is) return std::invoke(FWD(vis), FWD(self).template raw<Is>())
        if constexpr (sizeof...(Ts) >= 1) { CASE(0); }
        if constexpr (sizeof...(Ts) >= 2) { CASE(1); }
        if constexpr (sizeof...(Ts) >= 3) { CASE(2); }
        if constexpr (sizeof...(Ts) >= 4) { CASE(3); }
        if constexpr (sizeof...(Ts) >= 5) { CASE(4); }
        if constexpr (sizeof...(Ts) >= 6) { CASE(5); }
        if constexpr (sizeof...(Ts) >= 7) { CASE(6); }
        if constexpr (sizeof...(Ts) == 8) { CASE(7); }
        std::unreachable();
    }
};