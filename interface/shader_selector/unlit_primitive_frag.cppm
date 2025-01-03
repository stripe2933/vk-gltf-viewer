export module vk_gltf_viewer:shader_selector.unlit_primitive_frag;

import std;
export import fastgltf;
import :shader.unlit_primitive_frag;
import :helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> unlit_primitive_frag(bool hasBaseColorTexture, fastgltf::AlphaMode alphaMode) {
        constexpr type_map hasBaseColorTextureMap {
            make_type_map_entry<std::integral_constant<int, 0>>(false),
            make_type_map_entry<std::integral_constant<int, 1>>(true),
        };
        constexpr type_map alphaModeMap {
            make_type_map_entry<std::integral_constant<int, 0>>(fastgltf::AlphaMode::Opaque),
            make_type_map_entry<std::integral_constant<int, 1>>(fastgltf::AlphaMode::Mask),
            make_type_map_entry<std::integral_constant<int, 2>>(fastgltf::AlphaMode::Blend),
        };
        return std::visit([]<int... Is>(std::type_identity<std::integral_constant<int, Is>>...) -> std::span<const unsigned int> {
            return shader::unlit_primitive_frag<Is...>;
        }, hasBaseColorTextureMap.get_variant(hasBaseColorTexture), alphaModeMap.get_variant(alphaMode));
    }
}
