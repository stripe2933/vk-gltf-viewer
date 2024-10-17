module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:helpers.fastgltf;

import std;
export import glm;
export import :helpers.cstring_view;
export import :helpers.optional;

namespace fastgltf {
    export template <typename T>
    [[nodiscard]] auto to_optional(OptionalWithFlagValue<T> v) noexcept -> std::optional<T> {
        return value_if(v.has_value(), [&]() { return *v; });
    }

    export
    [[nodiscard]] auto to_string(PrimitiveType value) noexcept -> cstring_view {
        switch (value) {
            case PrimitiveType::Points: return "Points";
            case PrimitiveType::Lines: return "Lines";
            case PrimitiveType::LineLoop: return "LineLoop";
            case PrimitiveType::LineStrip: return "LineStrip";
            case PrimitiveType::Triangles: return "Triangles";
            case PrimitiveType::TriangleStrip: return "TriangleStrip";
            case PrimitiveType::TriangleFan: return "TriangleFan";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(AccessorType value) noexcept -> cstring_view {
        switch (value) {
            case AccessorType::Invalid: return "Invalid";
            case AccessorType::Scalar: return "Scalar";
            case AccessorType::Vec2: return "Vec2";
            case AccessorType::Vec3: return "Vec3";
            case AccessorType::Vec4: return "Vec4";
            case AccessorType::Mat2: return "Mat2";
            case AccessorType::Mat3: return "Mat3";
            case AccessorType::Mat4: return "Mat4";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(ComponentType value) noexcept -> cstring_view {
        switch (value) {
            case ComponentType::Byte: return "Byte";
            case ComponentType::UnsignedByte: return "UnsignedByte";
            case ComponentType::Short: return "Short";
            case ComponentType::UnsignedShort: return "UnsignedShort";
            case ComponentType::UnsignedInt: return "UnsignedInt";
            case ComponentType::Float: return "Float";
            case ComponentType::Invalid: return "Invalid";
            case ComponentType::Int: return "Int";
            case ComponentType::Double: return "Double";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(BufferTarget target) noexcept -> cstring_view {
        switch (target) {
            case BufferTarget::ArrayBuffer: return "ArrayBuffer";
            case BufferTarget::ElementArrayBuffer: return "ElementArrayBuffer";
            default: return "-";
        }
    }

    export
    [[nodiscard]] auto to_string(MimeType mime) noexcept -> cstring_view {
        switch (mime) {
            case MimeType::None: return "-";
            case MimeType::JPEG: return "image/jpeg";
            case MimeType::PNG: return "image/png";
            case MimeType::KTX2: return "image/ktx2";
            case MimeType::GltfBuffer: return "model/gltf-buffer";
            case MimeType::OctetStream: return "application/octet-stream";
            case MimeType::DDS: return "image/vnd-ms.dds";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(AlphaMode alphaMode) noexcept -> cstring_view {
        switch (alphaMode) {
            case AlphaMode::Opaque: return "Opaque";
            case AlphaMode::Mask: return "Mask";
            case AlphaMode::Blend: return "Blend";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(Filter filter) noexcept -> cstring_view {
        switch (filter) {
            case Filter::Linear: return "Linear";
            case Filter::Nearest: return "Nearest";
            case Filter::LinearMipMapLinear: return "LinearMipMapLinear";
            case Filter::LinearMipMapNearest: return "LinearMipMapNearest";
            case Filter::NearestMipMapLinear: return "NearestMipMapLinear";
            case Filter::NearestMipMapNearest: return "NearestMipMapNearest";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(Wrap wrap) noexcept -> cstring_view {
        switch (wrap) {
            case Wrap::Repeat: return "Repeat";
            case Wrap::ClampToEdge: return "ClampToEdge";
            case Wrap::MirroredRepeat: return "MirroredRepeat";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] glm::mat4 toMatrix(const Node::TransformMatrix &transformMatrix) noexcept {
        return glm::make_mat4(transformMatrix.data());
    }

    export
    [[nodiscard]] glm::mat4 toMatrix(const TRS &trs) noexcept {
        return translate(glm::mat4 { 1.f }, glm::make_vec3(trs.translation.data()))
            * mat4_cast(glm::make_quat(trs.rotation.data()))
            * scale(glm::mat4 { 1.f }, glm::make_vec3(trs.scale.data()));
    }
}
