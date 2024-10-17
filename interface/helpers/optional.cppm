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