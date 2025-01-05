export module vk_gltf_viewer:shader_selector.unlit_primitive_vert;

import std;
export import fastgltf;
import :shader.unlit_primitive_vert;
import :helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> unlit_primitive_vert(bool hasBaseColorTexture, bool hasColorAttribute) {
        constexpr type_map hasBaseColorTextureMap {
            make_type_map_entry<std::integral_constant<int, 0>>(false),
            make_type_map_entry<std::integral_constant<int, 1>>(true),
        };
        constexpr type_map hasColorAttributeMap {
            make_type_map_entry<std::integral_constant<int, 0>>(false),
            make_type_map_entry<std::integral_constant<int, 1>>(true),
        };
        return std::visit([]<int... Is>(std::type_identity<std::integral_constant<int, Is>>...) -> std::span<const unsigned int> {
            return shader::unlit_primitive_vert<Is...>;
        }, hasBaseColorTextureMap.get_variant(hasBaseColorTexture), hasColorAttributeMap.get_variant(hasColorAttribute));
    }
}
