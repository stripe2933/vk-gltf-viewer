module;

#include <fastgltf/types.hpp>

module vk_gltf_viewer;
import :helpers.enum_to_string;

auto vk_gltf_viewer::to_string(fastgltf::PrimitiveType value) noexcept -> cstring_view {
    switch (value) {
        case fastgltf::PrimitiveType::Points: return "Points";
        case fastgltf::PrimitiveType::Lines: return "Lines";
        case fastgltf::PrimitiveType::LineLoop: return "LineLoop";
        case fastgltf::PrimitiveType::LineStrip: return "LineStrip";
        case fastgltf::PrimitiveType::Triangles: return "Triangles";
        case fastgltf::PrimitiveType::TriangleStrip: return "TriangleStrip";
        case fastgltf::PrimitiveType::TriangleFan: return "TriangleFan";
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::AccessorType value) noexcept -> cstring_view {
    switch (value) {
        case fastgltf::AccessorType::Invalid: return "Invalid";
        case fastgltf::AccessorType::Scalar: return "Scalar";
        case fastgltf::AccessorType::Vec2: return "Vec2";
        case fastgltf::AccessorType::Vec3: return "Vec3";
        case fastgltf::AccessorType::Vec4: return "Vec4";
        case fastgltf::AccessorType::Mat2: return "Mat2";
        case fastgltf::AccessorType::Mat3: return "Mat3";
        case fastgltf::AccessorType::Mat4: return "Mat4";
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::ComponentType value) noexcept -> cstring_view {
    switch (value) {
        case fastgltf::ComponentType::Byte: return "Byte";
        case fastgltf::ComponentType::UnsignedByte: return "UnsignedByte";
        case fastgltf::ComponentType::Short: return "Short";
        case fastgltf::ComponentType::UnsignedShort: return "UnsignedShort";
        case fastgltf::ComponentType::UnsignedInt: return "UnsignedInt";
        case fastgltf::ComponentType::Float: return "Float";
        case fastgltf::ComponentType::Invalid: return "Invalid";
        case fastgltf::ComponentType::Int: return "Int";
        case fastgltf::ComponentType::Double: return "Double";
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::BufferTarget target) noexcept -> cstring_view {
    switch (target) {
        case fastgltf::BufferTarget::ArrayBuffer: return "ArrayBuffer";
        case fastgltf::BufferTarget::ElementArrayBuffer: return "ElementArrayBuffer";
        default: return "-";
    }
}

auto vk_gltf_viewer::to_string(fastgltf::MimeType mime) noexcept -> cstring_view {
    switch (mime) {
        case fastgltf::MimeType::None: return "-";
        case fastgltf::MimeType::JPEG: return "image/jpeg";
        case fastgltf::MimeType::PNG: return "image/png";
        case fastgltf::MimeType::KTX2: return "image/ktx2";
        case fastgltf::MimeType::GltfBuffer: return "model/gltf-buffer";
        case fastgltf::MimeType::OctetStream: return "application/octet-stream";
        case fastgltf::MimeType::DDS: return "image/vnd-ms.dds";
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::AlphaMode alphaMode) noexcept -> cstring_view {
    switch (alphaMode) {
        case fastgltf::AlphaMode::Opaque: return "Opaque";
        case fastgltf::AlphaMode::Mask: return "Mask";
        case fastgltf::AlphaMode::Blend: return "Blend";
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::Filter filter) noexcept -> cstring_view {
    switch (filter) {
        case fastgltf::Filter::Linear: return "Linear";
        case fastgltf::Filter::Nearest: return "Nearest";
        case fastgltf::Filter::LinearMipMapLinear: return "LinearMipMapLinear";
        case fastgltf::Filter::LinearMipMapNearest: return "LinearMipMapNearest";
        case fastgltf::Filter::NearestMipMapLinear: return "NearestMipMapLinear";
        case fastgltf::Filter::NearestMipMapNearest: return "NearestMipMapNearest";
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::Wrap wrap) noexcept -> cstring_view {
    switch (wrap) {
        case fastgltf::Wrap::Repeat: return "Repeat";
        case fastgltf::Wrap::ClampToEdge: return "ClampToEdge";
        case fastgltf::Wrap::MirroredRepeat: return "MirroredRepeat";
    }
    std::unreachable();
}