export module math;

import std;

namespace math {
    /**
     * @brief Get the ceiling of the division of two integers.
     * @tparam T Integer type.
     * @param num Numerator.
     * @param denom Denominator. MUST be nonzero.
     * @return Ceiling of the division of \p num by \p denom.
     */
    export template <std::integral T>
    [[nodiscard]] constexpr T divCeil(T num, T denom) noexcept {
        return (num / denom) + (num % denom != 0);
    }

    /**
     * @brief Get the square of an integer.
     * @tparam T Integer type.
     * @param n Integer to square.
     * @return Square of \p n.
     */
    export template <std::integral T>
    [[nodiscard]] constexpr T square(T n) noexcept {
        return n * n;
    }
}
