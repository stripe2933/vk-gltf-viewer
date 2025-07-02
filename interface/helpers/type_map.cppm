export module vk_gltf_viewer.helpers.type_map;

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
 * // ./main 3 -> std::out_of_range
 * @endcode
 *
 * @tparam K Key type.
 * @tparam Vs Value types.
 */
export template <typename K, typename... Vs>
struct type_map : type_map_entry<Vs, K>...{
    /**
     * @brief Get variant of <tt>std::type_identity<Vs>...</tt> that is storing the type matching to the given \p key.
     * @param key Key to get the value.
     * @return Variant of <tt>std::type_identity<Vs>...</tt>.
     * @throw std::out_of_range If the key is not found.
     */
    [[nodiscard]] constexpr std::variant<std::type_identity<Vs>...> get_variant(K key) const {
        std::variant<std::type_identity<Vs>...> result;
        [&, this]<std::size_t... Is>(std::index_sequence<Is...>){
            // If there is no key in the type_map, the below fold expression will be evaluated as false.
            if (!((key == static_cast<const type_map_entry<Vs, K>*>(this)->key ? (result.template emplace<Is>(), true) : false) || ...)) {
                throw std::out_of_range { "type_map::get_variant" };
            }
        }(std::make_index_sequence<sizeof...(Vs)>{});
        return result;
    }
};

/**
 * @brief Convenience type map for monotonic integer sequence starting from <tt>Start</tt> (default by 0).
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
 * // ./main 0 1 2 -> std::out_of_range (argc > 3)
 * @endcode
 *
 * @tparam Count The count value of the iota sequence.
 * @tparam Start Start value. Default is 0.
 */
export template <auto Count, decltype(Count) Start = 0>
struct iota_map {
    /**
     * @brief Get variant of <tt>std::integral_constant<decltype(Count), Is>...</tt> that is storing the type matching to the given \p key.
     * @param key Key to get the value.
     * @return Variant of <tt>std::integral_constant<decltype(Count), Is>...</tt>.
     * @throw std::out_of_range If the key is not found.
     */
    [[nodiscard]] static constexpr auto get_variant(decltype(Start + Count) key) {
        return INTEGER_SEQ(Is, Count, {
            std::variant<std::integral_constant<decltype(Start + Is), Start + Is>...> result;
            if (!((key == Start + Is ? (result.template emplace<Is>(), true) : false) || ...)) {
                throw std::out_of_range { "iota_map::get_variant" };
            }
            return result;
        });
    }
};