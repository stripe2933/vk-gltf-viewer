export module vk_gltf_viewer:shader_selector.primitive_frag;

import std;
import :shader.primitive_frag;

import vk_gltf_viewer.helpers.type_map;

namespace vk_gltf_viewer::shader_selector {
    export
    [[nodiscard]] std::span<const unsigned int> primitive_frag(int TEXCOORD_COUNT, int HAS_COLOR_ATTRIBUTE, int FRAGMENT_SHADER_GENERATED_TBN, int ALPHA_MODE, int EXT_SHADER_STENCIL_EXPORT) {
        constexpr iota_map<5> texcoordCountMap;
        constexpr iota_map<2> hasColorAttributeMap;
        constexpr iota_map<2> fragmentShaderGeneratedTBNMap;
        constexpr iota_map<3> alphaModeMap;
        constexpr iota_map<2> extShaderStencilExportMap;
        return std::visit(
            [](auto ...Is) -> std::span<const unsigned int> {
                return shader::primitive_frag<Is...>;
            },
            texcoordCountMap.get_variant(TEXCOORD_COUNT),
            hasColorAttributeMap.get_variant(HAS_COLOR_ATTRIBUTE),
            fragmentShaderGeneratedTBNMap.get_variant(FRAGMENT_SHADER_GENERATED_TBN),
            alphaModeMap.get_variant(ALPHA_MODE),
            extShaderStencilExportMap.get_variant(EXT_SHADER_STENCIL_EXPORT));
    }
}
