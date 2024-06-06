module;

#include <tuple>

export module vku:utils.RefHolder;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vku{
    export template <typename T, typename... Ts>
    struct RefHolder {
        std::tuple<Ts...> temporaryValues;
        T value;

        template <std::invocable<Ts&...> F>
        RefHolder(
            F &&f,
            auto &&...temporaryValues
        ) : temporaryValues { FWD(temporaryValues)... },
            value { std::apply(FWD(f), this->temporaryValues) } { }

        constexpr operator T&() noexcept { return value; }
        constexpr operator const T&() const noexcept { return value; }

        constexpr auto get() noexcept -> T& { return value; }
        constexpr auto get() const noexcept -> const T& { return value; }
    };

    template <typename F, typename... Ts>
    RefHolder(F &&, Ts &&...) -> RefHolder<std::invoke_result_t<F, Ts&...>, Ts...>;
}