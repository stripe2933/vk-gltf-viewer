export module vk_gltf_viewer.helpers.Lazy;

import std;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

/**
 * @brief A container that wraps the value and lazily calculates it when needed.
 *
 * @tparam T Value type that be stored and lazily calculated.
 * @tparam F Function type that calculates the value when the value is invalidated.
 */
export template <typename T, std::invocable F = std::function<T()>>
class Lazy {
    std::optional<T> value;
    F calculator;

public:
    explicit Lazy(F &&f) noexcept(std::is_nothrow_constructible_v<F, F&&>)
        : calculator { FWD(f) } { }

    void invalidate() noexcept {
        value.reset();
    }

    [[nodiscard]] const T &get() noexcept {
        if (!value) {
            value.emplace(std::invoke(calculator));
        }
        return *value;
    }
};

export template <std::invocable F>
explicit Lazy(F&&) -> Lazy<std::invoke_result_t<F>, F>;