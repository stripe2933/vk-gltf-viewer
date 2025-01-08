export module vk_gltf_viewer:shader_selector.unlit_primitive_vert;

import std;
export import fastgltf;
import :shader.unlit_primitive_vert;
import :helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> unlit_primitive_vert(int HAS_BASE_COLOR_TEXTURE, int HAS_COLOR_ATTRIBUTE) {
        constexpr iota_map<2> hasBaseColorTextureMap;
        constexpr iota_map<2> hasColorAttributeMap;
        return std::visit(
            [](auto... Is) -> std::span<const unsigned int> {
                return shader::unlit_primitive_vert<Is...>;
            },
            hasBaseColorTextureMap.get_variant(HAS_BASE_COLOR_TEXTURE),
            hasColorAttributeMap.get_variant(HAS_COLOR_ATTRIBUTE));
    }
}
