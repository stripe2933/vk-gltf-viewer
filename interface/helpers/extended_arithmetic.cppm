export module vk_gltf_viewer:helpers.extended_arithmetic;

import std;

namespace vk_gltf_viewer::inline helpers {
    export template <std::unsigned_integral T>
    [[nodiscard]] constexpr auto divCeil(T num, T denom) noexcept -> T {
        return (num / denom) + (num % denom != 0);
    }
}