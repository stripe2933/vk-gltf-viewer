export module vk_gltf_viewer:shader_selector.primitive_frag;

import std;
import :shader.primitive_frag;

import vk_gltf_viewer.helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> primitive_frag(int TEXCOORD_COUNT, int HAS_COLOR_0_ATTRIBUTE, int FRAGMENT_SHADER_GENERATED_TBN, int ALPHA_MODE, int EXT_SHADER_STENCIL_EXPORT) {
        return std::visit(
            [](auto ...Is) -> std::span<const unsigned int> {
                return shader::primitive_frag<Is...>;
            },
            iota_map<5>::get_variant(TEXCOORD_COUNT),
            iota_map<2>::get_variant(HAS_COLOR_0_ATTRIBUTE),
            iota_map<2>::get_variant(FRAGMENT_SHADER_GENERATED_TBN),
            iota_map<3>::get_variant(ALPHA_MODE),
            iota_map<2>::get_variant(EXT_SHADER_STENCIL_EXPORT));
    }
}
