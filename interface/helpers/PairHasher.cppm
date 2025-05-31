module;

#include <boost/container_hash/hash.hpp>

export module vk_gltf_viewer.helpers.PairHasher;

import std;

export struct PairHasher {
    template <typename T1, typename T2>
    [[nodiscard]] std::size_t operator()(const std::pair<T1, T2> &p) const noexcept {
        std::size_t seed = 0;
        boost::hash_combine(seed, p.first);
        boost::hash_combine(seed, p.second);
        return seed;
    }
};