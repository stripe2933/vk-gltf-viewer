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

#define HAS_VARIADIC_OUT !FRAGMENT_SHADER_GENERATED_TBN || TEXCOORD_COUNT >= 1 || HAS_COLOR_ATTRIBUTE

layout (constant_id = 0) const uint PACKED_ATTRIBUTE_COMPONENT_TYPES = 0;
layout (constant_id = 1) const uint COLOR_COMPONENT_COUNT = 0;
layout (constant_id = 2) const uint MORPH_TARGET_WEIGHT_COUNT = 0;
layout (constant_id = 3) const uint PACKED_MORPH_TARGET_AVAILABILITY = 0;
layout (constant_id = 4) const uint SKIN_ATTRIBUTE_COUNT = 0;

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

#if HAS_COLOR_ATTRIBUTE
    vec4 color;
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

    vec3 inPosition = getPosition(PACKED_ATTRIBUTE_COMPONENT_TYPES & 0xFU, (PACKED_MORPH_TARGET_AVAILABILITY & 0x1U) == 0x1U ? MORPH_TARGET_WEIGHT_COUNT : 0U);
    outPosition = (transform * vec4(inPosition, 1.0)).xyz;

    outMaterialIndex = MATERIAL_INDEX;

#if !FRAGMENT_SHADER_GENERATED_TBN
    vec3 inNormal = getNormal((PACKED_ATTRIBUTE_COMPONENT_TYPES >> 4U) & 0xFU, (PACKED_MORPH_TARGET_AVAILABILITY & 0x2U) == 0x2U ? MORPH_TARGET_WEIGHT_COUNT : 0U);
    variadic_out.tbn[2] = normalize(mat3(transform) * inNormal); // N

    if (MATERIAL.normalTextureIndex != 0US){
        vec4 inTangent = getTangent((PACKED_ATTRIBUTE_COMPONENT_TYPES >> 8U) & 0xFU, (PACKED_MORPH_TARGET_AVAILABILITY & 0x4U) == 0x4U ? MORPH_TARGET_WEIGHT_COUNT : 0U);
        variadic_out.tbn[0] = normalize(mat3(transform) * inTangent.xyz); // T
        variadic_out.tbn[1] = cross(variadic_out.tbn[2], variadic_out.tbn[0]) * -inTangent.w; // B
    }
#endif

#if TEXCOORD_COUNT == 1
    variadic_out.texcoord = getTexcoord(0, (PACKED_ATTRIBUTE_COMPONENT_TYPES >> 12U) & 0xFU);
#elif TEXCOORD_COUNT >= 2
    for (uint i = 0; i < TEXCOORD_COUNT; i++){
        variadic_out.texcoords[i] = getTexcoord(i, (PACKED_ATTRIBUTE_COMPONENT_TYPES >> (12U + 4U * i)) & 0xFU);
    }
#endif

#if HAS_COLOR_ATTRIBUTE
    variadic_out.color = getColor(PACKED_ATTRIBUTE_COMPONENT_TYPES >> 28U);
#endif

    gl_Position = pc.projectionView * vec4(outPosition, 1.0);
    gl_PointSize = 1.0;
}