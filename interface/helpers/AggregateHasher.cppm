module;

#include <boost/container_hash/hash.hpp>

export module vk_gltf_viewer:helpers.AggregateHasher;

import std;
import reflect;

export struct AggregateHasher {
    template <typename T> requires std::is_aggregate_v<T>
    [[nodiscard]] constexpr std::size_t operator()(const T &v) const noexcept {
        return reflect::visit([](const auto &...fields) {
            std::size_t seed = 0;
            (boost::hash_combine(seed, fields), ...);
            return seed;
        }, v);
    }
};