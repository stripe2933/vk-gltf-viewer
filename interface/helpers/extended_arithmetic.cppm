export module vk_gltf_viewer:helpers.extended_arithmetic;

import std;
export import glm;

export template <std::unsigned_integral T>
[[nodiscard]] constexpr auto divCeil(T num, T denom) noexcept -> T {
    return (num / denom) + (num % denom != 0);
}

export template <std::floating_point T, glm::qualifier Q>
[[nodiscard]] constexpr auto toEuclideanCoord(const glm::vec<4, T, Q> &homogeneousCoord) noexcept -> glm::vec<3, T, Q> {
    return glm::vec<3, T, Q> { homogeneousCoord } / homogeneousCoord.w;
}