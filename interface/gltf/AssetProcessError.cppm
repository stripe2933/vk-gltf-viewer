export module vk_gltf_viewer:gltf.AssetProcessError;

import std;
export import cstring_view;

namespace vk_gltf_viewer::gltf {
    export enum class AssetProcessError : std::uint8_t {
        SparseAttributeBufferAccessor,     /// Attribute buffer accessor is sparse.
        NormalizedAttributeBufferAccessor, /// Attribute buffer accessor is normalized.
        TooLargeAccessorByteStride,        /// The byte stride of the accessor is too large that is cannot be represented in 8-byte unsigned integer.
        IndeterminateImageMimeType,        /// Image MIME type cannot be determined (neither provided nor inferred from the file extension).
        UnsupportedSourceDataType,         /// The source data type is not supported.
    };

    export cpp_util::cstring_view to_string(AssetProcessError error) noexcept {
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
        std::unreachable();
    }
}