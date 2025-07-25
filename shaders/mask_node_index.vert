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

#define HAS_VARIADIC_OUT HAS_BASE_COLOR_TEXTURE || HAS_COLOR_0_ALPHA_ATTRIBUTE

layout (constant_id = 0) const uint POSITION_COMPONENT_TYPE = 0;
layout (constant_id = 1) const bool POSITION_NORMALIZED = false;
layout (constant_id = 2) const uint BASE_COLOR_TEXCOORD_COMPONENT_TYPE = 0;
layout (constant_id = 3) const bool BASE_COLOR_TEXCOORD_NORMALIZED = false;
layout (constant_id = 4) const uint COLOR_0_COMPONENT_TYPE = 0;
layout (constant_id = 5) const uint POSITION_MORPH_TARGET_COUNT = 0;
layout (constant_id = 6) const uint SKIN_ATTRIBUTE_COUNT = 0;

layout (location = 0) flat out uint outNodeIndex;
layout (location = 1) flat out uint outMaterialIndex;
#if HAS_VARIADIC_OUT
layout (location = 2) out VS_VARIADIC_OUT {
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord;
#endif
#if HAS_COLOR_0_ALPHA_ATTRIBUTE
    float color0Alpha;
#endif
} variadic_out;
#endif

layout (set = 0, binding = 0, std430) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 0, binding = 1, std430) readonly buffer NodeBuffer {
    Node nodes[];
};
layout (set = 0, binding = 2, std430) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
} pc;

#include "vertex_pulling.glsl"
#include "transform.glsl"

void main(){
    outNodeIndex = NODE_INDEX;
    outMaterialIndex = MATERIAL_INDEX;
#if HAS_BASE_COLOR_TEXTURE
    variadic_out.baseColorTexcoord = getTexcoord(uint(MATERIAL.baseColorTexcoordIndex), BASE_COLOR_TEXCOORD_COMPONENT_TYPE, BASE_COLOR_TEXCOORD_NORMALIZED);
#endif
#if HAS_COLOR_0_ALPHA_ATTRIBUTE
    variadic_out.color0Alpha = getColor0Alpha(COLOR_0_COMPONENT_TYPE);
#endif

    vec3 inPosition = getPosition(POSITION_COMPONENT_TYPE, POSITION_NORMALIZED, POSITION_MORPH_TARGET_COUNT);
    gl_Position = pc.projectionView * getTransform(SKIN_ATTRIBUTE_COUNT) * vec4(inPosition, 1.0);
    gl_PointSize = 1.0;
}