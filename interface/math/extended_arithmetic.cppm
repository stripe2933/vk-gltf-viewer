module;

#include <cassert>

export module vk_gltf_viewer:math.extended_arithmetic;

import std;
export import glm;

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

    /**
     * @brief Convert a 4-dimensional homogeneous coordinate vector to the 3-dimensional Euclidean coordinate vector.
     * @tparam T Type of coordinates.
     * @tparam Q GLM qualifier.
     * @param homogeneousCoord 4-dimensional homogeneous coordinate vector. The w component MUST be nonzero.
     * @return 3-dimensional Euclidean coordinate vector.
     */
    export template <std::floating_point T, glm::qualifier Q>
    [[nodiscard]] constexpr glm::vec<3, T, Q> toEuclideanCoord(const glm::vec<4, T, Q> &homogeneousCoord) NOEXCEPT_IF_RELEASE {
        assert(homogeneousCoord.w != 0 && "Can't divide by zero!");
        return glm::vec<3, T, Q> { homogeneousCoord } / homogeneousCoord.w;
    }
}