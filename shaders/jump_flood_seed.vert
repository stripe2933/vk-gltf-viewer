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

layout (constant_id = 0) const uint POSITION_COMPONENT_TYPE = 0;
layout (constant_id = 1) const uint POSITION_MORPH_TARGET_WEIGHT_COUNT = 0;
layout (constant_id = 2) const uint SKIN_ATTRIBUTE_COUNT = 0;

layout (set = 0, binding = 0, std430) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 0, binding = 1, std430) readonly buffer NodeBuffer {
    Node nodes[];
};

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
} pc;

#include "vertex_pulling.glsl"
#include "transform.glsl"

void main(){
    vec3 inPosition = getPosition(POSITION_COMPONENT_TYPE, POSITION_MORPH_TARGET_WEIGHT_COUNT);
    gl_Position = pc.projectionView * getTransform(SKIN_ATTRIBUTE_COUNT) * vec4(inPosition, 1.0);
    gl_PointSize = 1.0;
}