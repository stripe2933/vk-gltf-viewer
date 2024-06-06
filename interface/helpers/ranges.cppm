module;

#include <concepts>
#include <ranges>
#include <type_traits>
#include <version>

export module vk_gltf_viewer:helpers.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vk_gltf_viewer::ranges {
    export template <typename Derived>
#if !defined(_LIBCPP_VERSION) && __cpp_lib_ranges >= 202202L // https://github.com/llvm/llvm-project/issues/70557#issuecomment-1851936055
        using range_adaptor_closure = std::ranges::range_adaptor_closure<Derived>;
#else
        requires std::is_object_v<Derived> && std::same_as<Derived, std::remove_cv_t<Derived>>
    struct range_adaptor_closure {
        template <std::ranges::range R>
        [[nodiscard]] friend constexpr auto operator|(
            R &&r, 
            const Derived &derived
        ) noexcept(std::is_nothrow_invocable_v<const Derived&, R>) {
            return derived(FWD(r));
        }
    };
#endif


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