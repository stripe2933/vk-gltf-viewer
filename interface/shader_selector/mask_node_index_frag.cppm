export module vk_gltf_viewer:shader_selector.mask_node_index_frag;

import std;
import :shader.mask_node_index_frag;
import :helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> mask_node_index_frag(int HAS_BASE_COLOR_TEXTURE, int HAS_COLOR_ALPHA_ATTRIBUTE) {
        constexpr iota_map<2> hasBaseColorTextureMap;
        constexpr iota_map<2> hasColorAlphaAttributeMap;
        return std::visit(
            [](auto... Is) -> std::span<const unsigned int> {
                return shader::mask_node_index_frag<Is...>;
            },
            hasBaseColorTextureMap.get_variant(HAS_BASE_COLOR_TEXTURE),
            hasColorAlphaAttributeMap.get_variant(HAS_COLOR_ALPHA_ATTRIBUTE));
    }
}
