export module vk_gltf_viewer.shader_selector.primitive_vert;

import std;

import vk_gltf_viewer.helpers.type_map;
import vk_gltf_viewer.shader.primitive_vert;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> primitive_vert(int TEXCOORD_COUNT, int HAS_COLOR_0_ATTRIBUTE, int FRAGMENT_SHADER_GENERATED_TBN);
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

std::span<const unsigned int> vk_gltf_viewer::shader_selector::primitive_vert(int TEXCOORD_COUNT, int HAS_COLOR_0_ATTRIBUTE, int FRAGMENT_SHADER_GENERATED_TBN) {
    return std::visit(
        [](auto ...Is) -> std::span<const unsigned int> {
            return shader::primitive_vert<Is...>;
        },
        iota_map<5>::get_variant(TEXCOORD_COUNT),
        iota_map<2>::get_variant(HAS_COLOR_0_ATTRIBUTE),
        iota_map<2>::get_variant(FRAGMENT_SHADER_GENERATED_TBN));
}