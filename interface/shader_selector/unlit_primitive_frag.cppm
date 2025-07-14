export module vk_gltf_viewer:shader_selector.unlit_primitive_frag;

import std;
import :shader.unlit_primitive_frag;

import vk_gltf_viewer.helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> unlit_primitive_frag(int HAS_BASE_COLOR_TEXTURE, int HAS_COLOR_0_ATTRIBUTE, int ALPHA_MODE) {
        return std::visit(
            [](auto... Is) -> std::span<const unsigned int> {
                return shader::unlit_primitive_frag<Is...>;
            },
            iota_map<2>::get_variant(HAS_BASE_COLOR_TEXTURE),
            iota_map<2>::get_variant(HAS_COLOR_0_ATTRIBUTE),
            iota_map<3>::get_variant(ALPHA_MODE));
    }
}
