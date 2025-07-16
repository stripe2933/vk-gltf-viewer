#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#define VERTEX_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_OUT !FRAGMENT_SHADER_GENERATED_TBN || TEXCOORD_COUNT >= 1 || HAS_COLOR_0_ATTRIBUTE

layout (constant_id =  0) const uint POSITION_COMPONENT_TYPE = 0;
layout (constant_id =  1) const bool POSITION_NORMALIZED = false;
layout (constant_id =  2) const uint NORMAL_COMPONENT_TYPE = 0;
layout (constant_id =  3) const uint TANGENT_COMPONENT_TYPE = 0;
layout (constant_id =  4) const uint TEXCOORD_0_COMPONENT_TYPE = 0;
layout (constant_id =  5) const uint TEXCOORD_1_COMPONENT_TYPE = 0;
layout (constant_id =  6) const uint TEXCOORD_2_COMPONENT_TYPE = 0;
layout (constant_id =  7) const uint TEXCOORD_3_COMPONENT_TYPE = 0;
layout (constant_id =  8) const bool TEXCOORD_0_NORMALIZED = false;
layout (constant_id =  9) const bool TEXCOORD_1_NORMALIZED = false;
layout (constant_id = 10) const bool TEXCOORD_2_NORMALIZED = false;
layout (constant_id = 11) const bool TEXCOORD_3_NORMALIZED = false;
layout (constant_id = 12) const uint COLOR_0_COMPONENT_TYPE = 0;
layout (constant_id = 13) const uint COLOR_0_COMPONENT_COUNT = 0;
layout (constant_id = 14) const uint POSITION_MORPH_TARGET_COUNT = 0;
layout (constant_id = 15) const uint NORMAL_MORPH_TARGET_COUNT = 0;
layout (constant_id = 16) const uint TANGENT_MORPH_TARGET_COUNT = 0;
layout (constant_id = 17) const uint SKIN_ATTRIBUTE_COUNT = 0;

layout (location = 0) out vec3 outPosition;
layout (location = 1) flat out uint outMaterialIndex;
#if HAS_VARIADIC_OUT
layout (location = 2) out VS_VARIADIC_OUT {
#if !FRAGMENT_SHADER_GENERATED_TBN
    mat3 tbn;
#endif

#if TEXCOORD_COUNT == 1
    vec2 texcoord;
#elif TEXCOORD_COUNT == 2
    mat2 texcoords;
#elif TEXCOORD_COUNT == 3
    mat3x2 texcoords;
#elif TEXCOORD_COUNT == 4
    mat4x2 texcoords;
#elif TEXCOORD_COUNT >= 5
#error "Maximum texcoord count exceeded."
#endif

#if HAS_COLOR_0_ATTRIBUTE
    vec4 color0;
#endif
} variadic_out;
#endif

layout (set = 1, binding = 0, std430) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 1, binding = 1, std430) readonly buffer NodeBuffer {
    Node nodes[];
};
layout (set = 1, binding = 2, std430) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
    vec3 viewPosition;
} pc;

#include "vertex_pulling.glsl"
#include "transform.glsl"

void main(){
    mat4 transform = getTransform(SKIN_ATTRIBUTE_COUNT);

    vec3 inPosition = getPosition(POSITION_COMPONENT_TYPE, POSITION_NORMALIZED, POSITION_MORPH_TARGET_COUNT);
    outPosition = (transform * vec4(inPosition, 1.0)).xyz;

    outMaterialIndex = MATERIAL_INDEX;

#if !FRAGMENT_SHADER_GENERATED_TBN
    vec3 inNormal = getNormal(NORMAL_COMPONENT_TYPE, NORMAL_MORPH_TARGET_COUNT);
    variadic_out.tbn[2] = normalize(mat3(transform) * inNormal); // N

    if (MATERIAL.normalTextureIndex != 0US){
        vec4 inTangent = getTangent(TANGENT_COMPONENT_TYPE, TANGENT_MORPH_TARGET_COUNT);
        variadic_out.tbn[0] = normalize(mat3(transform) * inTangent.xyz); // T
        variadic_out.tbn[1] = cross(variadic_out.tbn[2], variadic_out.tbn[0]) * -inTangent.w; // B
    }
#endif

#if TEXCOORD_COUNT == 1
    variadic_out.texcoord = getTexcoord(0, TEXCOORD_0_COMPONENT_TYPE, TEXCOORD_0_NORMALIZED);
#elif TEXCOORD_COUNT >= 2
    variadic_out.texcoords[0] = getTexcoord(0, TEXCOORD_0_COMPONENT_TYPE, TEXCOORD_0_NORMALIZED);
    variadic_out.texcoords[1] = getTexcoord(1, TEXCOORD_1_COMPONENT_TYPE, TEXCOORD_1_NORMALIZED);
#if TEXCOORD_COUNT >= 3
    variadic_out.texcoords[2] = getTexcoord(2, TEXCOORD_2_COMPONENT_TYPE, TEXCOORD_2_NORMALIZED);
#endif
#if TEXCOORD_COUNT == 4
    variadic_out.texcoords[3] = getTexcoord(3, TEXCOORD_3_COMPONENT_TYPE, TEXCOORD_3_NORMALIZED);
#endif
#endif

#if HAS_COLOR_0_ATTRIBUTE
    variadic_out.color0 = getColor0(COLOR_0_COMPONENT_TYPE, COLOR_0_COMPONENT_COUNT);
#endif

    gl_Position = pc.projectionView * vec4(outPosition, 1.0);
    gl_PointSize = 1.0;
}