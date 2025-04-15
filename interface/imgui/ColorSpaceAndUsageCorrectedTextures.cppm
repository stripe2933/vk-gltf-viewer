export module vk_gltf_viewer:imgui.ColorSpaceAndUsageCorrectedTextures;

import std;
export import imgui;

namespace vk_gltf_viewer::imgui {
    /**
     * @brief ImGui textures that are aware of the destination rendering color attachment color space and their usages.
     *
     * If ImGui UI is rendered to a color attachment with sRGB color space, non-sRGB textures must be treated as sRGB,
     * otherwise it will be rendered much vividly than expected. Conversely, if color attachment is not sRGB, sRGB
     * textures must be treated as non-sRGB, otherwise it will be rendered as washed out.
     *
     * Also, if texture is shown for specific material property, such like metallic/roughness/occlusion, only the relevant
     * channel should be shown. Since metallic roughness texture and occlusion texture can be combined as a single
     * texture, image view should be swizzled to propagate the channel to the whole RGB channels, and alpha value must
     * remain as 1.0.
     *
     * <tt>getTextureID</tt> returns ImGui texture with corrected color space, and
     * <tt>get{Metallic,Roughness,Normal,Occlusion,Emissive}TextureID</tt> returns ImGui texture with corrected color
     * space and usage. Specifically,
     *
     * - <tt>getMetallicTextureID</tt> returns the texture that propagates the blue channel,
     * - <tt>getRoughnessTextureID</tt> returns the texture that propagates the green channel,
     * - <tt>getOcclusionTextureID</tt> returns the texture that propagates the red channel.
     *
     * Note that since base color texture uses all RGBA channels, you can use <tt>getTextureID</tt> when such a things
     * like <tt>getBaseColorTextureID</tt> is needed.
     */
    export struct ColorSpaceAndUsageCorrectedTextures {
        virtual ~ColorSpaceAndUsageCorrectedTextures() = default;

        [[nodiscard]] virtual ImTextureID getTextureID(std::size_t textureIndex) const = 0;
        [[nodiscard]] virtual ImTextureID getMetallicTextureID(std::size_t materialIndex) const = 0;
        [[nodiscard]] virtual ImTextureID getRoughnessTextureID(std::size_t materialIndex) const = 0;
        [[nodiscard]] virtual ImTextureID getNormalTextureID(std::size_t materialIndex) const = 0;
        [[nodiscard]] virtual ImTextureID getOcclusionTextureID(std::size_t materialIndex) const = 0;
        [[nodiscard]] virtual ImTextureID getEmissiveTextureID(std::size_t materialIndex) const = 0;
    };
}