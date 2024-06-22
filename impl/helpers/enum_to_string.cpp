module;

#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <fastgltf/types.hpp>

module vk_gltf_viewer;
import :helpers.enum_to_string;

#define ENUM_TO_STRING_CASE(R, EnumType, EnumValue) case EnumType::EnumValue: return BOOST_PP_STRINGIZE(EnumValue);
#define ENUM_TO_STRING(EnumType, ...) \
    auto vk_gltf_viewer::to_string(EnumType value) noexcept -> const char* { \
        switch (value) { \
            BOOST_PP_SEQ_FOR_EACH(ENUM_TO_STRING_CASE, EnumType, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \
        } \
        std::unreachable(); \
    }

ENUM_TO_STRING(fastgltf::PrimitiveType, Points, Lines, LineLoop, LineStrip, Triangles, TriangleStrip, TriangleFan);
ENUM_TO_STRING(fastgltf::AccessorType, Invalid, Scalar, Vec2, Vec3, Vec4, Mat2, Mat3, Mat4);
ENUM_TO_STRING(fastgltf::ComponentType, Byte, UnsignedByte, Short, UnsignedShort, UnsignedInt, Float, Invalid, Int, Double);