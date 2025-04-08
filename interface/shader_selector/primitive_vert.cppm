export module vk_gltf_viewer:shader_selector.primitive_vert;

import std;
import :shader.primitive_vert;
import :helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> primitive_vert(int TEXCOORD_COUNT, int HAS_COLOR_ATTRIBUTE, int FRAGMENT_SHADER_GENERATED_TBN) {
        constexpr iota_map<5> texcoordCountMap;
        constexpr iota_map<2> hasColorAttributeMap;
        constexpr iota_map<2> fragmentShaderGeneratedTBNMap;
        return std::visit(
            [](auto ...Is) -> std::span<const unsigned int> {
                return shader::primitive_vert<Is...>;
            },
            texcoordCountMap.get_variant(TEXCOORD_COUNT),
            hasColorAttributeMap.get_variant(HAS_COLOR_ATTRIBUTE),
            fragmentShaderGeneratedTBNMap.get_variant(FRAGMENT_SHADER_GENERATED_TBN));
    }
}
