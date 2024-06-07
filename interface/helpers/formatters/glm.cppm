module;

#include <format>

export module vk_gltf_viewer:helpers.formatters.glm;

export import glm;

export template <std::size_t L, typename T, glm::qualifier Q>
struct std::formatter<glm::vec<L, T, Q>> : formatter<T> {
    constexpr auto format(const glm::vec<L, T, Q> &v, auto &ctx) const {
        format_to(ctx.out(), "(");
        formatter<T>::format(v.x, ctx);
        if constexpr (L >= 2) {
            format_to(ctx.out(), ", ");
            formatter<T>::format(v.y, ctx);
        }
        if constexpr (L >= 3) {
            format_to(ctx.out(), ", ");
            formatter<T>::format(v.z, ctx);
        }
        if constexpr (L >= 4) {
            format_to(ctx.out(), ", ");
            formatter<T>::format(v.w, ctx);
        }
        return format_to(ctx.out(), ")");
    }
};