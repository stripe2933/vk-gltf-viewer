module;

#include <concepts>
#include <ranges>
#include <type_traits>
#include <version>

export module vk_gltf_viewer:helpers.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vk_gltf_viewer::ranges {
    export template <typename D> requires
        std::is_object_v<D> && std::same_as<D, std::remove_cv_t<D>>
    struct range_adaptor_closure {
        template <std::ranges::range R>
        [[nodiscard]] friend constexpr auto operator|(R &&r, const D &derived) noexcept(std::is_nothrow_invocable_v<const D&, R>) {
            return derived(FWD(r));
        }
    };

namespace views {
#if __cpp_lib_ranges_enumerate >= 202302L
    export constexpr decltype(std::views::enumerate) enumerate;
#else
    struct enumerate_fn : range_adaptor_closure<enumerate_fn> {
        template <std::ranges::viewable_range R>
        static constexpr auto operator()(
            R &&r
        ) -> auto {
            if constexpr (std::ranges::sized_range<R>) {
                return std::views::zip(
                    std::views::iota(static_cast<std::ranges::range_difference_t<R>>(0), static_cast<std::ranges::range_difference_t<R>>(r.size())),
                    FWD(r));
            }
            else {
                return std::views::zip(
                    std::views::iota(static_cast<std::ranges::range_difference_t<R>>(0)),
                    FWD(r));
            }
        }
    };

    export constexpr enumerate_fn enumerate;
#endif
}
}