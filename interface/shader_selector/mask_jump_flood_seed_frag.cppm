export module vk_gltf_viewer:shader_selector.mask_jump_flood_seed_frag;

import std;
export import fastgltf;
import :shader.mask_jump_flood_seed_frag;
import :helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> mask_jump_flood_seed_frag(bool hasBaseColorTexture, bool hasColorAlphaAttribute) {
        constexpr type_map hasBaseColorTextureMap {
            make_type_map_entry<std::integral_constant<int, 0>>(false),
            make_type_map_entry<std::integral_constant<int, 1>>(true),
        };
        constexpr type_map hasColorAlphaAttributeMap {
            make_type_map_entry<std::integral_constant<int, 0>>(false),
            make_type_map_entry<std::integral_constant<int, 1>>(true),
        };
        return std::visit([]<int... Is>(std::type_identity<std::integral_constant<int, Is>>...) -> std::span<const unsigned int> {
            return shader::mask_jump_flood_seed_frag<Is...>;
        }, hasBaseColorTextureMap.get_variant(hasBaseColorTexture), hasColorAlphaAttributeMap.get_variant(hasColorAlphaAttribute));
    }
}
