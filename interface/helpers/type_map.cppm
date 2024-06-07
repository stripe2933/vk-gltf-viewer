module;

#include <concepts>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <variant>

export module vk_gltf_viewer:helpers.type_map;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vk_gltf_viewer {
    export template <typename T, typename V>
    struct type_map_entry{
        using type = T;
        V value;
    };

    export template <typename T, typename V>
    constexpr auto make_type_map_entry(V &&value) noexcept -> type_map_entry<T, V>{
        return { FWD(value) };
    }

    export template <typename MappingV, typename... MappingTs>
    class type_map{
    public:
        std::tuple<type_map_entry<MappingTs, MappingV>...> entries;

        constexpr type_map(type_map_entry<MappingTs, MappingV>... entries) noexcept
            : entries { std::move(entries)... } { }

        [[nodiscard]] constexpr auto get_monostate_variant(
            std::convertible_to<MappingV> auto &&value
        ) const noexcept -> std::variant<std::monostate, std::type_identity<MappingTs>...>{
            return get_monostate_variant<type_map_entry<MappingTs, MappingV>...>(FWD(value), entries);
        }

        [[nodiscard]] constexpr auto get_variant(
            std::convertible_to<MappingV> auto &&value
        ) const -> std::variant<std::type_identity<MappingTs>...>{
            return get_variant<type_map_entry<MappingTs, MappingV>...>(FWD(value), entries);
        }

    private:
        template <typename T, typename...>
        struct first_type { using type = T; };
        template <typename... Ts>
        using first_type_t = typename first_type<Ts...>::type;

        template <typename Ts1, typename... Ts>
        [[nodiscard]] static constexpr auto skip_front(
            std::tuple<Ts1, Ts...> tuple
        ) noexcept {
            return std::apply([](auto, auto ...tail) {
                return std::tuple { tail... };
            }, tuple);
        }

        template <typename ...Mappings>
        [[nodiscard]] constexpr auto get_monostate_variant(
            std::convertible_to<MappingV> auto &&value,
            std::tuple<Mappings...> entries
        ) const noexcept -> std::variant<std::monostate, std::type_identity<typename std::remove_cvref_t<Mappings>::type>...> {
            if (value == std::get<0>(entries).value) {
                return std::type_identity<typename first_type_t<std::remove_cvref_t<Mappings>...>::type>{};
            }

            if constexpr (sizeof...(Mappings) == 1){
                return std::monostate{};
            }
            else return std::visit(
                [](auto &&value) -> std::variant<std::monostate, std::type_identity<typename std::remove_cvref_t<Mappings>::type>...> {
                    return FWD(value);
                },
                get_monostate_variant(FWD(value), skip_front(entries))
            );
        }

        template <typename ...Mappings>
        [[nodiscard]] constexpr auto get_variant(
            std::convertible_to<MappingV> auto &&value,
            std::tuple<Mappings...> entries
        ) const -> std::variant<std::type_identity<typename std::remove_cvref_t<Mappings>::type>...> {
            if (value == std::get<0>(entries).value) {
                return std::type_identity<typename first_type_t<std::remove_cvref_t<Mappings>...>::type>{};
            }

            if constexpr (sizeof...(Mappings) == 1){
                throw std::runtime_error { "No mapping found for the given value." };
            }
            else return std::visit(
                [](auto &&value) -> std::variant<std::type_identity<typename std::remove_cvref_t<Mappings>::type>...> {
                    return std::move(value);
                },
                get_variant(FWD(value), skip_front(entries))
            );
        }
    };
};