export module vk_gltf_viewer:shader_selector.unlit_primitive_frag;

import std;
import :shader.unlit_primitive_frag;
import :helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> unlit_primitive_frag(int HAS_BASE_COLOR_TEXTURE, int HAS_COLOR_ATTRIBUTE, int ALPHA_MODE) {
        constexpr iota_map<2> hasBaseColorTextureMap;
        constexpr iota_map<2> hasColorAttributeMap;
        constexpr iota_map<3> alphaModeMap;
        return std::visit(
            [](auto... Is) -> std::span<const unsigned int> {
                return shader::unlit_primitive_frag<Is...>;
            },
            hasBaseColorTextureMap.get_variant(HAS_BASE_COLOR_TEXTURE),
            hasColorAttributeMap.get_variant(HAS_COLOR_ATTRIBUTE),
            alphaModeMap.get_variant(ALPHA_MODE));
    }
}
