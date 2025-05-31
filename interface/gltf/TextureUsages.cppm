export module vk_gltf_viewer.gltf.TextureUsages;

import std;
export import cstring_view;
export import fastgltf;

export import vk_gltf_viewer.helpers.Flags;
import vk_gltf_viewer.helpers.ranges;

namespace vk_gltf_viewer::gltf {
    export enum class TextureUsage : std::uint8_t {
        BaseColor = 1,
        MetallicRoughness = 2,
        Normal = 4,
        Occlusion = 8,
        Emissive = 16,
    };

    export struct TextureUsages : std::vector<std::unordered_map<std::size_t, Flags<TextureUsage>>> {
        explicit TextureUsages(const fastgltf::Asset &asset) {
            resize(asset.textures.size());
            for (const auto &[i, material] : asset.materials | ranges::views::enumerate) {
                if (material.pbrData.baseColorTexture) {
                    operator[](material.pbrData.baseColorTexture->textureIndex)[i] |= TextureUsage::BaseColor;
                }
                if (material.pbrData.metallicRoughnessTexture) {
                    operator[](material.pbrData.metallicRoughnessTexture->textureIndex)[i] |= TextureUsage::MetallicRoughness;
                }
                if (material.normalTexture) {
                    operator[](material.normalTexture->textureIndex)[i] |= TextureUsage::Normal;
                }
                if (material.occlusionTexture) {
                    operator[](material.occlusionTexture->textureIndex)[i] |= TextureUsage::Occlusion;
                }
                if (material.emissiveTexture) {
                    operator[](material.emissiveTexture->textureIndex)[i] |= TextureUsage::Emissive;
                }
            }
        }
    };

    [[nodiscard]] constexpr cpp_util::cstring_view to_string(TextureUsage usage) {
        switch (usage) {
            case TextureUsage::BaseColor: return "BaseColor";
            case TextureUsage::MetallicRoughness: return "MetallicRoughness";
            case TextureUsage::Normal: return "Normal";
            case TextureUsage::Occlusion: return "Occlusion";
            case TextureUsage::Emissive: return "Emissive";
        }
        std::unreachable();
    }
} // namespace vk_gltf_viewer::gltf

export template <>
struct std::formatter<vk_gltf_viewer::gltf::TextureUsage> : formatter<string_view> {
    auto format(vk_gltf_viewer::gltf::TextureUsage type, auto &ctx) const {
        return formatter<string_view>::format(to_string(type), ctx);
    }
};

export template <>
struct FlagTraits<vk_gltf_viewer::gltf::TextureUsage> {
    static constexpr bool isBitmask = true;
    static constexpr Flags<vk_gltf_viewer::gltf::TextureUsage> allFlags
        = vk_gltf_viewer::gltf::TextureUsage::BaseColor
        | vk_gltf_viewer::gltf::TextureUsage::MetallicRoughness
        | vk_gltf_viewer::gltf::TextureUsage::Normal
        | vk_gltf_viewer::gltf::TextureUsage::Occlusion
        | vk_gltf_viewer::gltf::TextureUsage::Emissive;
};