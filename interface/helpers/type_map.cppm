export module vk_gltf_viewer:helpers.type_map;

import std;

template <typename V, typename K>
struct type_map_entry {
    K key;
    using value_type = V;
};

export template <typename V, typename K>
[[nodiscard]] constexpr type_map_entry<V, K> make_type_map_entry(K key) noexcept {
    return { key };
}

/**
 * @brief Make a template-based runtime value to compile time type mapping.
 *
 * @code
 * int main(int argc, char **argv) {
 *     constexpr type_map map {
 *         make_type_map_entry<char>(0),
 *         make_type_map_entry<int>(1),
 *         make_type_map_entry<double>(2),
 *     };
 *
 *     return visit([]<typename T>(std::type_identity<T>) {
 *         return sizeof(T);
 *     }, map.get_variant(std::stoi(argv[1])));
 * }
 *
 * // ./main 0 -> return 1 (sizeof(char) = 1)
 * // ./main 1 -> return 4 (sizeof(int) = 4)
 * // ./main 2 -> return 8 (sizeof(double) = 8)
 * // ./main 3 -> std::runtime_error { "No mapping found for the given key." }
 * @endcode
 *
 * @tparam K Key type.
 * @tparam Vs Value types.
 */
export template <typename K, typename... Vs>
struct type_map : type_map_entry<Vs, K>...{
    [[nodiscard]] constexpr std::variant<std::type_identity<Vs>...> get_variant(K key) const {
        return get_variant<type_map_entry<Vs, K>...>(key);
    }

private:
    template <typename Mapping, typename ...Mappings>
    constexpr std::variant<std::type_identity<Vs>...> get_variant(K key) const {
        if (key == Mapping::key) {
            return std::type_identity<typename Mapping::value_type>{};
        }

        if constexpr (sizeof...(Mappings) == 0){
            throw std::runtime_error { "No mapping found for the given key." };
        }
        else {
            return get_variant<Mappings...>(key);
        }
    }
};