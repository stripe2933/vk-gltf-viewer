module;

#include <version>

export module ranges:contains;

import std;

namespace ranges {
#if __cpp_lib_ranges_contains >= 202207L
    export constexpr decltype(std::ranges::contains) contains;
    export constexpr decltype(std::ranges::contains_subrange) contains_subrange;
#else
    // Reference implementations are from cppreference, https://en.cppreference.com/w/cpp/algorithm/ranges/contains.
    // Note that this implementation does not support list-initialization of the value to search for (which proposed as
    // P2248R8, P3217R0 for C++26).

    struct __contains_fn
    {
        template< std::input_iterator I, std::sentinel_for<I> S,
                  class T, class Proj = std::identity >
        requires std::indirect_binary_predicate<std::ranges::equal_to, std::projected<I, Proj>,
                                                const T*>
        constexpr bool operator()(I first, S last, const T& value, Proj proj = {}) const
        {
            return std::ranges::find(std::move(first), last, value, proj) != last;
        }

        template< std::ranges::input_range R, class T, class Proj = std::identity >
        requires std::indirect_binary_predicate<std::ranges::equal_to,
                                                std::projected<std::ranges::iterator_t<R>, Proj>,
                                                const T*>
        constexpr bool operator()(R&& r, const T& value, Proj proj = {}) const
        {
            return (*this)(std::ranges::begin(r), std::ranges::end(r), std::move(value), proj);
        }
    };
    export constexpr __contains_fn contains {};

    struct __contains_subrange_fn
    {
        template< std::forward_iterator I1, std::sentinel_for<I1> S1,
                  std::forward_iterator I2, std::sentinel_for<I2> S2,
                  class Pred = std::ranges::equal_to,
                  class Proj1 = std::identity, class Proj2 = std::identity >
        requires std::indirectly_comparable<I1, I2, Pred, Proj1, Proj2>
        constexpr bool operator()(I1 first1, S1 last1,
                                  I2 first2, S2 last2,
                                  Pred pred = {},
                                  Proj1 proj1 = {}, Proj2 proj2 = {}) const
        {
            return (first2 == last2) ||
                   !std::ranges::search(first1, last1, first2, last2, pred, proj1, proj2).empty();
        }
 
        template< std::ranges::forward_range R1, std::ranges::forward_range R2,
                  class Pred = std::ranges::equal_to,
                  class Proj1 = std::identity, class Proj2 = std::identity >
        requires std::indirectly_comparable<std::ranges::iterator_t<R1>,
                                            std::ranges::iterator_t<R2>, Pred, Proj1, Proj2>
        constexpr bool operator()(R1&& r1, R2&& r2,
                                  Pred pred = {},
                                  Proj1 proj1 = {}, Proj2 proj2 = {}) const
        {
            return (*this)(std::ranges::begin(r1), std::ranges::end(r1),
                           std::ranges::begin(r2), std::ranges::end(r2), std::move(pred),
                           std::move(proj1), std::move(proj2));
        }
    };
    export constexpr __contains_subrange_fn contains_subrange {};
#endif
}