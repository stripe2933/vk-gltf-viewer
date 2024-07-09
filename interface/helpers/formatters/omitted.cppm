module;

#include <version>

export module vk_gltf_viewer:helpers.formatters.omitted;

import std;

namespace vk_gltf_viewer {
    export template <std::ranges::range R>
        requires (std::ranges::sized_range<R> || std::ranges::bidirectional_range<R>)
    struct omitted{
        std::ranges::ref_view<R> view;
        std::size_t n_head = 2;
        std::size_t n_tail = 2;
        bool show_size = true;

        explicit constexpr omitted(R &r) noexcept : view { r } { }
    };
}

#if __cpp_lib_format_ranges >= 202207L
export template <typename R>
struct std::formatter<vk_gltf_viewer::omitted<R>> : range_formatter<std::ranges::range_value_t<R>>{
    constexpr formatter() noexcept{
        range_formatter<std::ranges::range_value_t<R>>::set_brackets("", "");
    }

    template <typename FormatContext>
    constexpr auto format(vk_gltf_viewer::omitted<R> x, FormatContext &ctx) const{
        if constexpr (std::ranges::sized_range<R>){
            format_to(ctx.out(), "[");

            if (x.view.size() <= x.n_head + x.n_tail){
                // If the size of the range is less than the number of elements to omit, just print the whole range.
                range_formatter<std::ranges::range_value_t<R>>::format(x.view, ctx);
            }
            else{
                auto head = x.view | std::views::take(x.n_head);
                range_formatter<std::ranges::range_value_t<R>>::format(head, ctx);

                format_to(ctx.out(), ", ..., ");

                auto tail = x.view | std::views::drop(x.view.size() - x.n_tail);
                range_formatter<std::ranges::range_value_t<R>>::format(tail, ctx);
            }

            if (x.show_size){
                return format_to(ctx.out(), "] (size={})", x.view.size());
            }
            else{
                return format_to(ctx.out(), "]");
            }
        }
        else{ // By the constraint of omitted, the range must be bidirectional.
            format_to(ctx.out(), "[");

            auto head = x.view | std::views::take(x.n_head);
            range_formatter<std::ranges::range_value_t<R>>::format(head, ctx);

            format_to(ctx.out(), ", ..., ");

            auto tail = x.view | std::views::reverse | std::views::take(x.n_tail) | std::views::reverse;
            range_formatter<std::ranges::range_value_t<R>>::format(tail, ctx);

            return format_to(ctx.out(), "]");
        }
    }
};
#endif