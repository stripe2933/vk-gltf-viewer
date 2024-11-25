module;

#include <cassert>

export module vk_gltf_viewer:math.extended_arithmetic;

import std;

#ifdef NDEBUG
#define NOEXCEPT_IF_RELEASE noexcept
#else
#define NOEXCEPT_IF_RELEASE
#endif

namespace vk_gltf_viewer::math {
    /**
     * @brief Calculate <tt>ceil(num / denom)</tt> in efficient manner.
     *
     * Instead of doing floating point arithmetic, it calculates the division and modulo operation by once.
     *
     * @tparam T Type of numerator and denominator.
     * @param num Numerator.
     * @param denom Denominator. MUST be nonzero.
     * @return ceil(num / denom).
     */
    export template <std::unsigned_integral T>
    [[nodiscard]] constexpr T divCeil(T num, T denom) NOEXCEPT_IF_RELEASE {
        assert(denom != 0 && "Can't divide by zero!");
        return (num / denom) + (num % denom != 0);
    }
}