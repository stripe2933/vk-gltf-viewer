module;

#include <boost/container_hash/hash.hpp>

export module vk_gltf_viewer:helpers.AggregateHasher;

import std;

export template <std::size_t FieldCount> requires (FieldCount >= 1 && FieldCount <= 8)
struct AggregateHasher {
    template <typename T> requires std::is_aggregate_v<T>
    [[nodiscard]] constexpr std::size_t operator()(const T &v) const noexcept {
        std::size_t seed = 0;
        if constexpr (FieldCount == 1) {
            const auto &[x1] = v;
            boost::hash_combine(seed, x1);
        }
        if constexpr (FieldCount == 2) {
            const auto &[x1, x2] = v;
            boost::hash_combine(seed, x1);
            boost::hash_combine(seed, x2);
        }
        if constexpr (FieldCount == 3) {
            const auto &[x1, x2, x3] = v;
            boost::hash_combine(seed, x1);
            boost::hash_combine(seed, x2);
            boost::hash_combine(seed, x3);
        }
        if constexpr (FieldCount == 4) {
            const auto &[x1, x2, x3, x4] = v;
            boost::hash_combine(seed, x1);
            boost::hash_combine(seed, x2);
            boost::hash_combine(seed, x3);
            boost::hash_combine(seed, x4);
        }
        if constexpr (FieldCount == 5) {
            const auto &[x1, x2, x3, x4, x5] = v;
            boost::hash_combine(seed, x1);
            boost::hash_combine(seed, x2);
            boost::hash_combine(seed, x3);
            boost::hash_combine(seed, x4);
            boost::hash_combine(seed, x5);
        }
        if constexpr (FieldCount == 6) {
            const auto &[x1, x2, x3, x4, x5, x6] = v;
            boost::hash_combine(seed, x1);
            boost::hash_combine(seed, x2);
            boost::hash_combine(seed, x3);
            boost::hash_combine(seed, x4);
            boost::hash_combine(seed, x5);
            boost::hash_combine(seed, x6);
        }
        if constexpr (FieldCount == 7) {
            const auto &[x1, x2, x3, x4, x5, x6, x7] = v;
            boost::hash_combine(seed, x1);
            boost::hash_combine(seed, x2);
            boost::hash_combine(seed, x3);
            boost::hash_combine(seed, x4);
            boost::hash_combine(seed, x5);
            boost::hash_combine(seed, x6);
            boost::hash_combine(seed, x7);
        }
        if constexpr (FieldCount == 8) {
            const auto &[x1, x2, x3, x4, x5, x6, x7, x8] = v;
            boost::hash_combine(seed, x1);
            boost::hash_combine(seed, x2);
            boost::hash_combine(seed, x3);
            boost::hash_combine(seed, x4);
            boost::hash_combine(seed, x5);
            boost::hash_combine(seed, x6);
            boost::hash_combine(seed, x7);
            boost::hash_combine(seed, x8);
        }
        return seed;
    }
};