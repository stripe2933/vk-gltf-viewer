export module vk_gltf_viewer.gltf.AssetProcessError;

import std;
export import cstring_view;

namespace vk_gltf_viewer::gltf {
    export enum class AssetProcessError : std::uint8_t {
        IndeterminateImageMimeType, /// Image MIME type cannot be determined (neither provided nor inferred from the file extension).
        UnsupportedSourceDataType,  /// The source data type is not supported.
        TooManyTextureError,        /// The number of textures exceeds the system GPU limit.
    };

    export cpp_util::cstring_view to_string(AssetProcessError error) noexcept {
        switch (error) {
            case AssetProcessError::IndeterminateImageMimeType:
                return "Image MIME type cannot be determined.";
            case AssetProcessError::UnsupportedSourceDataType:
                return "The source data type is not supported.";
            case AssetProcessError::TooManyTextureError:
                return "The number of textures exceeds the system GPU limit.";
        }
        std::unreachable();
    }
}