module;

#include <ranges>
#include <version>

export module vku:details.ranges;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { (Is, __VA_ARGS__)... }; })
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vku::ranges {
    export template <typename Derived>
#if __cpp_lib_ranges >= 202202L
        using range_adaptor_closure = std::ranges::range_adaptor_closure<Derived>;
#else
        requires std::is_object_v<Derived>&& std::same_as<Derived, std::remove_cv_t<Derived>>
    struct range_adaptor_closure {
        template <std::ranges::range R>
        [[nodiscard]] friend constexpr auto operator|(
            R&& r,
            const Derived& derived
            ) noexcept(std::is_nothrow_invocable_v<const Derived&, R>) {
            return derived(FWD(r));
}
    };
#endif

    export template <std::size_t N>
    struct to_array : range_adaptor_closure<to_array<N>> {
        template <std::ranges::input_range R>
        [[nodiscard]] constexpr auto operator()(
            R &&r
        ) const -> std::array<std::ranges::range_value_t<R>, N> {
            auto it = r.begin();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
            return ARRAY_OF(N, *it++);
#pragma clang diagnostic pop
        }
    };

    namespace views {
#if __cpp_lib_ranges_zip >= 202110L
        export constexpr decltype(std::views::zip_transform) zip_transform;
#else
        export constexpr auto zip_transform(
            auto &&f,
            std::ranges::input_range auto &&...rs
        ) -> auto {
            return std::views::zip(FWD(rs)...) | std::views::transform([&](auto &&t) {
                return std::apply(f, FWD(t));
            });
        }
#endif
    }
};