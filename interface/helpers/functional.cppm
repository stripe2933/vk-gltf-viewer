export module vk_gltf_viewer:helpers.functional;

import std;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vk_gltf_viewer::inline helpers {
    export template <typename ...Fs>
    struct multilambda : Fs... {
        using Fs::operator()...;
    };

    /**
     * Make a lambda function that automatically decomposes the structure binding of an input parameter.
     * For example, your input parameter type is <tt>std::tuple<std::string, int, double></tt>, the lambda declaration would be:
     * @code
     * [](const auto &tuple) {
     *     const auto &[arg1, arg2, arg3] = tuple;
     *     // (Use arg1, arg2 and arg3)
     * })
     * @endcode
     * With <tt>decomposer, you can avoid the nested decomposition by like:
     * @code
     * decomposer([](const std::string &arg1, int arg2, double arg3) {
     * // (Use arg1, arg2 and arg3)
     * })
     * @endcode
     * @param f Function that accepts the decomposed structure bindings.
     * @return Function which accepts the composed argument and executes \p f with decomposition.
     */
    export template <typename F>
    [[nodiscard]] auto decomposer(F &&f) noexcept(std::is_nothrow_move_constructible_v<F>) {
        return [f = FWD(f)](auto &&tuple) {
            return std::apply(f, FWD(tuple));
        };
    }

    /**
     * Visit \p v as \p T, and return the result.
     * @tparam T Visited type.
     * @tparam Ts Types of \p v's alternatives. These types must be convertible to \p T.
     * @param v Variant to visit.
     * @return Visited value.
     * @example
     * @code
     * visit_as<float>(std::variant<int, float>{ 3 }); // Returns 3.f
     * @endcode
     */
    export template <typename T, std::convertible_to<T>... Ts>
    [[nodiscard]] auto visit_as(const std::variant<Ts...> &v) -> T {
        return std::visit([](T x) { return x; }, v);
    }
}