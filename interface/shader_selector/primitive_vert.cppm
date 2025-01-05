export module vk_gltf_viewer:shader_selector.primitive_vert;

import std;
import :shader.primitive_vert;
import :helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    [[nodiscard]] std::span<const unsigned int> primitive_vert(std::uint8_t texcoordCount, bool hasColorAttribute, bool fragmentShaderGeneratedTBN) {
        constexpr type_map texcoordCountMap {
            make_type_map_entry<std::integral_constant<int, 0>, std::uint8_t>(0),
            make_type_map_entry<std::integral_constant<int, 1>, std::uint8_t>(1),
            make_type_map_entry<std::integral_constant<int, 2>, std::uint8_t>(2),
            make_type_map_entry<std::integral_constant<int, 3>, std::uint8_t>(3),
            make_type_map_entry<std::integral_constant<int, 4>, std::uint8_t>(4),
        };
        constexpr type_map hasColorAttributeMap {
            make_type_map_entry<std::integral_constant<int, 0>, std::uint8_t>(0),
            make_type_map_entry<std::integral_constant<int, 1>, std::uint8_t>(1),
        };
        constexpr type_map fragmentShaderGeneratedTBNMap {
            make_type_map_entry<std::integral_constant<int, 0>>(false),
            make_type_map_entry<std::integral_constant<int, 1>>(true),
        };
        return std::visit([]<int... Is>(std::type_identity<std::integral_constant<int, Is>>...) -> std::span<const unsigned int> {
            return shader::primitive_vert<Is...>;
        }, texcoordCountMap.get_variant(texcoordCount), hasColorAttributeMap.get_variant(hasColorAttribute), fragmentShaderGeneratedTBNMap.get_variant(fragmentShaderGeneratedTBN));
    }
}
