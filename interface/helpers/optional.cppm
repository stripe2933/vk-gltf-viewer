export module vk_gltf_viewer:helpers.optional;

import std;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

/**
 * @brief Create <tt>std::optional</tt> with \p value, if \p condition is true.
 *
 * Intended for converting a bool to an optional value. Since <tt>condition ? value : std::nullopt</tt> requires <tt>value</tt>
 * to be an optional type, this function helps to create it without explicitly specifying the type.
 *
 * This always requires \p value to be instantiated. If the value should be created only when the condition is
 * <tt>true</tt>, use value_if(bool, F&&) instead.
 *
 * @tparam T
 * @param condition Condition whether create the optional value or not.
 * @param value Value to create the optional.
 * @return <tt>std::optional</tt> with \p value, if \p condition is <tt>true</tt>; otherwise, <tt>std::nullopt</tt>.
 */
export template <typename T>
[[nodiscard]] std::optional<std::remove_cvref_t<T>> value_if(bool condition, T &&value) {
    if (condition) {
        return FWD(value);
    }
    return std::nullopt;
}

/**
 * @brief Create <tt>std::optional</tt> with the execution result of \p f, if \p condition is true.
 *
 * Intended for converting a bool to an optional value. Since <tt>condition ? value : std::nullopt</tt> requires <tt>value</tt>
 * to be an optional type and always instantiate the value, this function is for the case where the value is only needed when
 * the \p condition is <tt>true</tt>.
 *
 * If constructing \p value is cheap, you can use value_if(bool, T&&) instead.
 *
 * @tparam F
 * @param condition Condition whether create the optional value or not.
 * @param f Function to create the value.
 * @return <tt>std::optional</tt> with the execution result of \p f, if \p condition is <tt>true</tt>; otherwise, <tt>std::nullopt</tt>.
 */
export template <std::invocable F>
[[nodiscard]] std::optional<std::invoke_result_t<F>> value_if(bool condition, F &&f) {
    if (condition) {
        return std::invoke(f);
    }
    return std::nullopt;
}

struct to_range_fn {
    template <typename T>
    constexpr auto operator()(const std::optional<T> &opt) const -> std::span<const T> {
        return { &*opt, opt.has_value() ? std::size_t{ 1 } : std::size_t{ 0 } };
    }

    template <typename T>
    constexpr auto operator()(std::optional<T> &opt) const -> std::span<T> {
        return { &*opt, opt.has_value() ? std::size_t{ 1 } : std::size_t{ 0 } };
    }
};

/**
 * @brief Convert <tt>std::optional</tt> to range.
 *
 * This is intended for future C++26's <Give std::optional Range support> (P3168) compatibility, but implemented as just
 * as stopgap solution (use <tt>std::span</tt> for simplicity).
 */
export constexpr to_range_fn to_range;