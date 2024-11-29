module;

#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__AUX__BASE
#define WUFFS_CONFIG__MODULE__AUX__IMAGE
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__JPEG
#define WUFFS_CONFIG__MODULE__PNG
#include <wuffs-unsupported-snapshot.c>

export module vk_gltf_viewer:io.Rgb2RgbaDecodeImageCallbacks;

namespace vk_gltf_viewer::io {
    /**
     * @brief Customized <tt>wuffs_aux::DecodeImageCallbacks</tt> that is preserving the image channels with Vulkan-friendly way.
     *
     * It uses raw data as the image source if channels are 1, 2 or 4, and pad alpha channel if channels are 3.
     */
    struct Rgb2RgbaDecodeImageCallbacks final : wuffs_aux::DecodeImageCallbacks {
        wuffs_base__pixel_format SelectPixfmt(const wuffs_base__image_config &image_config) override {
            switch (image_config.pixcfg.pixel_format().bits_per_pixel()) {
                case 8U:
                    return { WUFFS_BASE__PIXEL_FORMAT__Y };
                case 16U:
                    return { WUFFS_BASE__PIXEL_FORMAT__YA_NONPREMUL };
                case 24U:
                case 32U:
                    return { WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL };
            }
            std::unreachable();
        }
    };
}