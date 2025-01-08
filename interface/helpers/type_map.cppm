export module vk_gltf_viewer:helpers.type_map;

import std;

#define INTEGER_SEQ(Is, N, ...) [&]<auto ...Is>(std::integer_sequence<decltype(N), Is...>) __VA_ARGS__ (std::make_integer_sequence<decltype(N), N>{})

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
        std::variant<std::type_identity<Vs>...> result;
        [&, this]<std::size_t... Is>(std::index_sequence<Is...>){
            std::ignore = ((key == static_cast<const type_map_entry<Vs, K>*>(this)->key ? (result.template emplace<Is>(), true) : false) || ...);
        }(std::make_index_sequence<sizeof...(Vs)>{});
        return result;
    }
};

/**
 * @brief Convenience type map for monotonic integer sequence starting from 0.
 *
 * For <tt>type_map</tt> with mapping the runtime integer value to same compile time integer value, use <tt>iota_map</tt>.
 *
 * @code
 * int main(int argc, char **argv) {
 *     constexpr iota_map<3> map;
 *     std::visit([](auto Is) {
 *         std::array<const char*, Is> arguments;
 *         std::copy_n(argv, arguments.size(), arguments.data());
 *         std::println("{::?}", arguments);
 *     }, map.get_variant(argc));
 * }
 *
 * // ./main 0 1 -> print ["./main", "0", "1"]
 * // ./main hello -> print ["./main", "hello"]
 * // ./main 0 1 2 -> std::runtime_error { "No mapping found for the given key." } (argc > 3)
 * @endcode
 *
 * @tparam Stop The stop value of the iota sequence.
 */
export template <auto Stop>
struct iota_map {
    explicit iota_map() = default;

    [[nodiscard]] constexpr auto get_variant(decltype(Stop) key) const {
        return INTEGER_SEQ(Is, Stop, {
            std::variant<std::integral_constant<decltype(Is), Is>...> result;
            std::ignore = ((key == Is ? (result.template emplace<Is>(), true) : false) || ...);
            return result;
        });
    }
};