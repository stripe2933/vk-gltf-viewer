export module vk_gltf_viewer.shader_selector.mask_node_index_vert;

import std;

import vk_gltf_viewer.helpers.type_map;
import vk_gltf_viewer.shader.mask_node_index_vert;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> mask_node_index_vert(int HAS_BASE_COLOR_TEXTURE, int HAS_COLOR_0_ALPHA_ATTRIBUTE);
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

std::span<const unsigned int> vk_gltf_viewer::shader_selector::mask_node_index_vert(int HAS_BASE_COLOR_TEXTURE, int HAS_COLOR_0_ALPHA_ATTRIBUTE) {
    return std::visit(
        [](auto... Is) -> std::span<const unsigned int> {
            return shader::mask_node_index_vert<Is...>;
        },
        iota_map<2>::get_variant(HAS_BASE_COLOR_TEXTURE),
        iota_map<2>::get_variant(HAS_COLOR_0_ALPHA_ATTRIBUTE));
}