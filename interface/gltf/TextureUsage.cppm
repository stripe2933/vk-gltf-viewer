export module vk_gltf_viewer:gltf.TextureUsage;

import std;
export import cstring_view;
export import fastgltf;
import :helpers.enums.Flags;
import :helpers.ranges;

namespace vk_gltf_viewer::gltf {
    class TextureUsage {
    public:
        enum class Type : std::uint8_t {
            BaseColor = 1,
            MetallicRoughness = 2,
            Normal = 4,
            Occlusion = 8,
            Emissive = 16,
        };

        TextureUsage(const fastgltf::Asset &asset, std::invocable<const fastgltf::Texture&> auto const &textureIndexGetter)
            : usages { asset.textures.size() } {
            for (const auto &[i, material] : asset.materials | ranges::views::enumerate) {
                if (material.pbrData.baseColorTexture) {
                    const fastgltf::Texture &texture = asset.textures[material.pbrData.baseColorTexture->textureIndex];
                    usages[textureIndexGetter(texture)][i] |= Type::BaseColor;
                }
                if (material.pbrData.metallicRoughnessTexture) {
                    const fastgltf::Texture &texture = asset.textures[material.pbrData.metallicRoughnessTexture->textureIndex];
                    usages[textureIndexGetter(texture)][i] |= Type::MetallicRoughness;
                }
                if (material.normalTexture) {
                    const fastgltf::Texture &texture = asset.textures[material.normalTexture->textureIndex];
                    usages[textureIndexGetter(texture)][i] |= Type::Normal;
                }
                if (material.occlusionTexture) {
                    const fastgltf::Texture &texture = asset.textures[material.occlusionTexture->textureIndex];
                    usages[textureIndexGetter(texture)][i] |= Type::Occlusion;
                }
                if (material.emissiveTexture) {
                    const fastgltf::Texture &texture = asset.textures[material.emissiveTexture->textureIndex];
                    usages[textureIndexGetter(texture)][i] |= Type::Emissive;
                }
            }
        }

        [[nodiscard]] std::vector<std::pair<std::size_t, Flags<Type>>> getUsages(std::size_t textureIndex) const {
            // TODO: too much conversion std::unordered_map -> std::vector with heap allocation
            return { std::from_range, usages.at(textureIndex) };
        }

    private:
        std::vector<std::unordered_map<std::size_t, Flags<Type>>> usages;
    };

    [[nodiscard]] constexpr cpp_util::cstring_view to_string(TextureUsage::Type type) {
        switch (type) {
        case TextureUsage::Type::BaseColor: return "BaseColor";
        case TextureUsage::Type::MetallicRoughness: return "MetallicRoughness";
        case TextureUsage::Type::Normal: return "Normal";
        case TextureUsage::Type::Occlusion: return "Occlusion";
        case TextureUsage::Type::Emissive: return "Emissive";
        }
        throw std::runtime_error { "Invalid Enum" };
    }
} // namespace vk_gltf_viewer::gltf

export template <>
struct std::formatter<vk_gltf_viewer::gltf::TextureUsage::Type> : formatter<string_view> {
    auto format(vk_gltf_viewer::gltf::TextureUsage::Type type, auto &ctx) const {
        return formatter<string_view>::format(to_string(type), ctx);
    }
};

export template <>
struct FlagTraits<vk_gltf_viewer::gltf::TextureUsage::Type> {
    static constexpr bool isBitmask = true;
    static constexpr Flags<vk_gltf_viewer::gltf::TextureUsage::Type> allFlags
        = vk_gltf_viewer::gltf::TextureUsage::Type::BaseColor
        | vk_gltf_viewer::gltf::TextureUsage::Type::MetallicRoughness
        | vk_gltf_viewer::gltf::TextureUsage::Type::Normal
        | vk_gltf_viewer::gltf::TextureUsage::Type::Occlusion
        | vk_gltf_viewer::gltf::TextureUsage::Type::Emissive;
};