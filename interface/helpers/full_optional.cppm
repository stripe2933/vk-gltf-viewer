export module vk_gltf_viewer:helpers.full_optional;

import std;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

struct unset_t { explicit constexpr unset_t() = default; };
export constexpr unset_t unset;

export template <std::default_initializable T>
class full_optional {
public:
    constexpr full_optional() noexcept(std::is_nothrow_constructible_v<T>) = default;
    constexpr full_optional(std::nullopt_t) noexcept(std::is_nothrow_constructible_v<T>) { }
    constexpr full_optional(const full_optional&) noexcept(std::is_nothrow_constructible_v<T>) = default;
    constexpr full_optional(full_optional&&) noexcept = default;
    constexpr explicit full_optional(std::in_place_t, auto &&...args) noexcept(std::is_nothrow_constructible_v<T, decltype(args)...>)
        : value { FWD(args)... }, _active { true } { }

    template <typename... Args>
        requires (sizeof...(Args) >= 1) // For sizeof...(Args) == 0, full_optional() or full_optional(std::nullopt_t) should be called.
    constexpr explicit(sizeof...(Args) == 1) full_optional(unset_t, Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : value { FWD(args)... }, _active { false } { }

    template <std::convertible_to<T> U>
    constexpr full_optional(U &&initial) noexcept(std::is_nothrow_constructible_v<T, U>)
        : value { FWD(initial) }, _active { true } { }

    constexpr full_optional& operator=(const full_optional&) noexcept(std::is_nothrow_copy_assignable_v<T>) = default;
    constexpr full_optional& operator=(full_optional&&) noexcept = default;

    [[nodiscard]] const T &operator*() const noexcept { return value; }
    [[nodiscard]] T &operator*() noexcept { return value; }

    [[nodiscard]] constexpr operator bool() const noexcept {
        return _active;
    }

    [[nodiscard]] const T *operator->() const noexcept { return &value; }
    [[nodiscard]] T *operator->() noexcept { return &value; }

    [[nodiscard]] const T &get() const {
        if (_active) return value;
        throw std::bad_optional_access{};
    }

    [[nodiscard]] T &get() {
        if (_active) return value;
        throw std::bad_optional_access{};
    }

    [[nodiscard]] T &raw() & noexcept { return value; }
    [[nodiscard]] const T &raw() const & noexcept { return value; }
    [[nodiscard]] T &&raw() && noexcept { return value; }
    [[nodiscard]] const T &&raw() const && noexcept { return value; }

    auto reset() -> void {
        _active = false;
    }

    [[nodiscard]] auto has_value() const noexcept -> bool { return _active; }
    auto set_active(bool active) noexcept -> void { _active = active; }

    [[nodiscard]] auto to_optional() const noexcept -> std::optional<T> { return _active ? std::make_optional(value) : std::nullopt; }

    template <typename U = std::remove_cv_t<T>>
    [[nodiscard]] constexpr T value_or(U &&default_value) const & {
        return _active ? value : FWD(default_value);
    }

    template <typename U = std::remove_cv_t<T>>
    [[nodiscard]] constexpr T value_or(U &&default_value) && {
        return _active ? value : FWD(default_value);
    }

private:
    T value{};
    bool _active{};
};
