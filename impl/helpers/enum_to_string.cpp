module;

#include <fastgltf/types.hpp>

module vk_gltf_viewer;
import :helpers.enum_to_string;

import std;

using namespace std::string_view_literals;

auto vk_gltf_viewer::to_string(fastgltf::PrimitiveType value) noexcept -> const char* {
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

auto vk_gltf_viewer::to_string(fastgltf::AccessorType value) noexcept -> std::string_view {
    switch (value) {
        case fastgltf::AccessorType::Invalid: return "Invalid"sv;
        case fastgltf::AccessorType::Scalar: return "Scalar"sv;
        case fastgltf::AccessorType::Vec2: return "Vec2"sv;
        case fastgltf::AccessorType::Vec3: return "Vec3"sv;
        case fastgltf::AccessorType::Vec4: return "Vec4"sv;
        case fastgltf::AccessorType::Mat2: return "Mat2"sv;
        case fastgltf::AccessorType::Mat3: return "Mat3"sv;
        case fastgltf::AccessorType::Mat4: return "Mat4"sv;
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::ComponentType value) noexcept -> std::string_view {
    switch (value) {
        case fastgltf::ComponentType::Byte: return "Byte"sv;
        case fastgltf::ComponentType::UnsignedByte: return "UnsignedByte"sv;
        case fastgltf::ComponentType::Short: return "Short"sv;
        case fastgltf::ComponentType::UnsignedShort: return "UnsignedShort"sv;
        case fastgltf::ComponentType::UnsignedInt: return "UnsignedInt"sv;
        case fastgltf::ComponentType::Float: return "Float"sv;
        case fastgltf::ComponentType::Invalid: return "Invalid"sv;
        case fastgltf::ComponentType::Int: return "Int"sv;
        case fastgltf::ComponentType::Double: return "Double"sv;
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::BufferTarget target) noexcept -> const char* {
    switch (target) {
        case fastgltf::BufferTarget::ArrayBuffer: return "ArrayBuffer";
        case fastgltf::BufferTarget::ElementArrayBuffer: return "ElementArrayBuffer";
        default: return "-";
    }
}

auto vk_gltf_viewer::to_string(fastgltf::MimeType mime) noexcept -> const char* {
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

auto vk_gltf_viewer::to_string(fastgltf::AlphaMode alphaMode) noexcept -> const char* {
    switch (alphaMode) {
        case fastgltf::AlphaMode::Opaque: return "Opaque";
        case fastgltf::AlphaMode::Mask: return "Mask";
        case fastgltf::AlphaMode::Blend: return "Blend";
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::Filter filter) noexcept -> std::string_view {
    switch (filter) {
        case fastgltf::Filter::Linear: return "Linear"sv;
        case fastgltf::Filter::Nearest: return "Nearest"sv;
        case fastgltf::Filter::LinearMipMapLinear: return "LinearMipMapLinear"sv;
        case fastgltf::Filter::LinearMipMapNearest: return "LinearMipMapNearest"sv;
        case fastgltf::Filter::NearestMipMapLinear: return "NearestMipMapLinear"sv;
        case fastgltf::Filter::NearestMipMapNearest: return "NearestMipMapNearest"sv;
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::Wrap wrap) noexcept -> std::string_view {
    switch (wrap) {
        case fastgltf::Wrap::Repeat: return "Repeat"sv;
        case fastgltf::Wrap::ClampToEdge: return "ClampToEdge"sv;
        case fastgltf::Wrap::MirroredRepeat: return "MirroredRepeat"sv;
    }
    std::unreachable();
}