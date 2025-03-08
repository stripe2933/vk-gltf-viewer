module;

#include <cassert>

export module vk_gltf_viewer:helpers.span;

import std;

/**
 * Convert the span of \p U to the span of \p T. The result span byte size must be same as the \p span's.
 * @tparam T Result span type.
 * @tparam U Source span type.
 * @tparam N Size of the source span. If it is not <tt>std::dynamic_extent</tt>, the result span will also have the compile time size.
 * @param span Source span.
 * @return Converted span.
 * @note Since the source and result span sizes must be same, <tt>span.size_bytes()</tt> must be divisible by <tt>sizeof(T)</tt>.
 */
export template <typename T, typename U, std::size_t N>
[[nodiscard]] auto reinterpret_span(std::span<U, N> span) {
    if constexpr (N == std::dynamic_extent) {
        assert(span.size_bytes() % sizeof(T) == 0 && "Span size mismatch: span of T does not fully fit into the current span.");
        return std::span { reinterpret_cast<T*>(span.data()), span.size_bytes() / sizeof(T) };
    }
    else {
        static_assert((N * sizeof(U)) % sizeof(T) == 0, "Span size mismatch: span of T does not fully fit into the current span.");
        return std::span<T, N * sizeof(U) / sizeof(T)> { reinterpret_cast<T*>(span.data()), N * sizeof(U) / sizeof(T) };
    }
}