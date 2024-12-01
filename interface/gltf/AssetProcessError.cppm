export module vk_gltf_viewer:gltf.AssetProcessError;

import std;
export import :helpers.cstring_view;

#define DEFINE_FORMATTER(Type) \
    export template <> \
    struct std::formatter<Type> : formatter<string_view> { \
        auto format(Type v, auto &ctx) const { \
            return formatter<string_view>::format(to_string(v), ctx); \
        } \
    }

namespace vk_gltf_viewer::gltf {
    export enum class AssetProcessError : std::uint8_t {
        SparseAttributeBufferAccessor,     /// Attribute buffer accessor is sparse.
        NormalizedAttributeBufferAccessor, /// Attribute buffer accessor is normalized.
        TooLargeAccessorByteStride,        /// The byte stride of the accessor is too large that is cannot be represented in 8-byte unsigned integer.
        IndeterminateImageMimeType,        /// Image MIME type cannot be determined (neither provided nor inferred from the file extension).
        UnsupportedSourceDataType,         /// The source data type is not supported.
    };

    export cstring_view to_string(AssetProcessError error) noexcept {
        switch (error) {
            case AssetProcessError::SparseAttributeBufferAccessor:
                return "Attribute buffer accessor is sparse.";
            case AssetProcessError::NormalizedAttributeBufferAccessor:
                return "Attribute buffer accessor is normalized.";
            case AssetProcessError::TooLargeAccessorByteStride:
                return "The byte stride of the accessor is too large.";
            case AssetProcessError::IndeterminateImageMimeType:
                return "Image MIME type cannot be determined.";
            case AssetProcessError::UnsupportedSourceDataType:
                return "The source data type is not supported.";
        }
    }
}

DEFINE_FORMATTER(vk_gltf_viewer::gltf::AssetProcessError);