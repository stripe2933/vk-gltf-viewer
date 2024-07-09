module;

#include <fastgltf/types.hpp>

module vk_gltf_viewer;
import :helpers.enum_to_string;

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

auto vk_gltf_viewer::to_string(fastgltf::AccessorType value) noexcept -> const char* {
    switch (value) {
        case fastgltf::AccessorType::Invalid: return "Invalid";
        case fastgltf::AccessorType::Scalar: return "Scalar";
        case fastgltf::AccessorType::Vec2: return "Vec2";
        case fastgltf::AccessorType::Vec3: return "Vec3";
        case fastgltf::AccessorType::Mat2: return "Mat2";
        case fastgltf::AccessorType::Mat3: return "Mat3";
        case fastgltf::AccessorType::Mat4: return "Mat4";
    }
    std::unreachable();
}

auto vk_gltf_viewer::to_string(fastgltf::ComponentType value) noexcept -> const char* {
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