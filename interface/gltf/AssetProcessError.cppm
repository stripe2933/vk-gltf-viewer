export module vk_gltf_viewer.gltf.AssetProcessError;

import std;
export import cstring_view;

namespace vk_gltf_viewer::gltf {
    export enum class AssetProcessError : std::uint8_t {
        UnsupportedSourceDataType,  /// The source data type is not supported.
        TooManyTextureError,        /// The number of textures exceeds the system GPU limit.
    };

    export cpp_util::cstring_view to_string(AssetProcessError error) noexcept;
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

cpp_util::cstring_view vk_gltf_viewer::gltf::to_string(AssetProcessError error) noexcept {
    switch (error) {
        case AssetProcessError::UnsupportedSourceDataType:
            return "The source data type is not supported.";
        case AssetProcessError::TooManyTextureError:
            return "The number of textures exceeds the system GPU limit.";
    }
    std::unreachable();
}