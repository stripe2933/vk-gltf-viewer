export module vk_gltf_viewer.helpers.vulkan;

import std;
export import vulkan_hpp;

/**
 * @brief Convert sRGB format to linear format, or vice versa.
 * @param format Format to convert. Must have the corresponding sRGB toggled format.
 * @return Corresponding sRGB toggled format.
 * @throw std::invalid_argument If the given format does not have the corresponding sRGB toggled format.
 */
export
[[nodiscard]] vk::Format convertSrgb(vk::Format format);

/**
 * @brief Check if \p format is sRGB format.
 * @param format Format to check.
 * @return <tt>true</tt> if \p format is sRGB format, <tt>false</tt> otherwise.
 */
export
[[nodiscard]] bool isSrgbFormat(vk::Format format) noexcept;

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk::Format convertSrgb(vk::Format format) {
    switch (format) {
        #define BIMAP(x, y) \
            case vk::Format::x: return vk::Format::y; \
            case vk::Format::y: return vk::Format::x
        BIMAP(eR8Unorm, eR8Srgb);
        BIMAP(eR8G8Unorm, eR8G8Srgb);
        BIMAP(eR8G8B8Unorm, eR8G8B8Srgb);
        BIMAP(eB8G8R8Unorm, eB8G8R8Srgb);
        BIMAP(eR8G8B8A8Unorm, eR8G8B8A8Srgb);
        BIMAP(eB8G8R8A8Unorm, eB8G8R8A8Srgb);
        BIMAP(eA8B8G8R8UnormPack32, eA8B8G8R8SrgbPack32);
        BIMAP(eBc1RgbUnormBlock, eBc1RgbSrgbBlock);
        BIMAP(eBc1RgbaUnormBlock, eBc1RgbaSrgbBlock);
        BIMAP(eBc2UnormBlock, eBc2SrgbBlock);
        BIMAP(eBc3UnormBlock, eBc3SrgbBlock);
        BIMAP(eBc7UnormBlock, eBc7SrgbBlock);
        BIMAP(eEtc2R8G8B8UnormBlock, eEtc2R8G8B8SrgbBlock);
        BIMAP(eEtc2R8G8B8A1UnormBlock, eEtc2R8G8B8A1SrgbBlock);
        BIMAP(eEtc2R8G8B8A8UnormBlock, eEtc2R8G8B8A8SrgbBlock);
        BIMAP(eAstc4x4UnormBlock, eAstc4x4SrgbBlock);
        BIMAP(eAstc5x4UnormBlock, eAstc5x4SrgbBlock);
        BIMAP(eAstc5x5UnormBlock, eAstc5x5SrgbBlock);
        BIMAP(eAstc6x5UnormBlock, eAstc6x5SrgbBlock);
        BIMAP(eAstc6x6UnormBlock, eAstc6x6SrgbBlock);
        BIMAP(eAstc8x5UnormBlock, eAstc8x5SrgbBlock);
        BIMAP(eAstc8x6UnormBlock, eAstc8x6SrgbBlock);
        BIMAP(eAstc8x8UnormBlock, eAstc8x8SrgbBlock);
        BIMAP(eAstc10x5UnormBlock, eAstc10x5SrgbBlock);
        BIMAP(eAstc10x6UnormBlock, eAstc10x6SrgbBlock);
        BIMAP(eAstc10x8UnormBlock, eAstc10x8SrgbBlock);
        BIMAP(eAstc10x10UnormBlock, eAstc10x10SrgbBlock);
        BIMAP(eAstc12x10UnormBlock, eAstc12x10SrgbBlock);
        BIMAP(eAstc12x12UnormBlock, eAstc12x12SrgbBlock);
        BIMAP(ePvrtc12BppUnormBlockIMG, ePvrtc12BppSrgbBlockIMG);
        BIMAP(ePvrtc14BppUnormBlockIMG, ePvrtc14BppSrgbBlockIMG);
        BIMAP(ePvrtc22BppUnormBlockIMG, ePvrtc22BppSrgbBlockIMG);
        BIMAP(ePvrtc24BppUnormBlockIMG, ePvrtc24BppSrgbBlockIMG);
        #undef BIMAP
        default:
            throw std::invalid_argument { "No corresponding conversion format" };
    }
}

bool isSrgbFormat(vk::Format format) noexcept {
    for (vk::Format candidate : {
        vk::Format::eR8Srgb, vk::Format::eR8G8Srgb, vk::Format::eR8G8B8Srgb, vk::Format::eB8G8R8Srgb,
        vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb, vk::Format::eA8B8G8R8SrgbPack32,
        vk::Format::eBc1RgbSrgbBlock, vk::Format::eBc1RgbaSrgbBlock, vk::Format::eBc2SrgbBlock,
        vk::Format::eBc3SrgbBlock, vk::Format::eBc7SrgbBlock, vk::Format::eEtc2R8G8B8SrgbBlock,
        vk::Format::eEtc2R8G8B8A1SrgbBlock, vk::Format::eEtc2R8G8B8A8SrgbBlock, vk::Format::eAstc4x4SrgbBlock,
        vk::Format::eAstc5x4SrgbBlock, vk::Format::eAstc5x5SrgbBlock, vk::Format::eAstc6x5SrgbBlock,
        vk::Format::eAstc6x6SrgbBlock, vk::Format::eAstc8x5SrgbBlock, vk::Format::eAstc8x6SrgbBlock,
        vk::Format::eAstc8x8SrgbBlock, vk::Format::eAstc10x5SrgbBlock, vk::Format::eAstc10x6SrgbBlock,
        vk::Format::eAstc10x8SrgbBlock, vk::Format::eAstc10x10SrgbBlock, vk::Format::eAstc12x10SrgbBlock,
        vk::Format::eAstc12x12SrgbBlock, vk::Format::ePvrtc12BppSrgbBlockIMG, vk::Format::ePvrtc14BppSrgbBlockIMG,
        vk::Format::ePvrtc22BppSrgbBlockIMG, vk::Format::ePvrtc24BppSrgbBlockIMG,
    }) {
        if (format == candidate) {
            return true;
        }
    }

    return false;
}