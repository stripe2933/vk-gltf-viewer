module;

#include <concepts>
#include <type_traits>

export module vku:details.type_traits;

namespace vku::type_traits {
    export template <typename T, typename... Ts>
    constexpr std::size_t leading_n = (0 + ... + std::same_as<T, Ts>);

    template <typename... Ts>
    using last_type = typename decltype((std::type_identity<Ts>{}, ...))::type; // TODO.CXX26: use pack indexing.
}