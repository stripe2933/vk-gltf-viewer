export module vk_gltf_viewer:shader_selector.primitive_frag;

import std;
export import fastgltf;
import :shader.primitive_frag;
import :helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    [[nodiscard]] std::span<const unsigned int> primitive_frag(std::uint8_t texcoordCount, bool fragmentShaderGeneratedTBN, fastgltf::AlphaMode alphaMode) {
        constexpr type_map texcoordCountMap {
            make_type_map_entry<std::integral_constant<int, 0>, std::uint8_t>(0),
            make_type_map_entry<std::integral_constant<int, 1>, std::uint8_t>(1),
            make_type_map_entry<std::integral_constant<int, 2>, std::uint8_t>(2),
            make_type_map_entry<std::integral_constant<int, 3>, std::uint8_t>(3),
            make_type_map_entry<std::integral_constant<int, 4>, std::uint8_t>(4),
        };
        constexpr type_map fragmentShaderGeneratedTBNMap {
            make_type_map_entry<std::integral_constant<int, 0>>(false),
            make_type_map_entry<std::integral_constant<int, 1>>(true),
        };
        constexpr type_map alphaModeMap {
            make_type_map_entry<std::integral_constant<int, 0>>(fastgltf::AlphaMode::Opaque),
            make_type_map_entry<std::integral_constant<int, 1>>(fastgltf::AlphaMode::Mask),
            make_type_map_entry<std::integral_constant<int, 2>>(fastgltf::AlphaMode::Blend),
        };
        return std::visit([]<int... Is>(std::type_identity<std::integral_constant<int, Is>>...) -> std::span<const unsigned int> {
            return shader::primitive_frag<Is...>;
        }, texcoordCountMap.get_variant(texcoordCount), fragmentShaderGeneratedTBNMap.get_variant(fragmentShaderGeneratedTBN), alphaModeMap.get_variant(alphaMode));
    }
}
