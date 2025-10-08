export module vk_gltf_viewer.math.bit;

import std;

namespace vk_gltf_viewer::math::bit {
    export
    [[nodiscard]] constexpr std::uint32_t ones(std::uint32_t bitCount) noexcept {
        return (1U << bitCount) - 1;
    }
}