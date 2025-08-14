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
layout (constant_id = 1) const bool POSITION_NORMALIZED = false;
layout (constant_id = 2) const uint POSITION_MORPH_TARGET_COUNT = 0;
layout (constant_id = 3) const uint SKIN_ATTRIBUTE_COUNT = 0;

layout (location = 0) flat out uint outNodeIndex;

layout (set = 0, binding = 0) uniform CameraBuffer {
    mat4 projectionViews[4];
} camera;

layout (set = 1, binding = 0, std430) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 1, binding = 1, std430) readonly buffer NodeBuffer {
    Node nodes[];
};

layout (push_constant) uniform PushConstant {
    uint viewIndex;
} pc;

#include "vertex_pulling.glsl"
#include "transform.glsl"

void main(){
    outNodeIndex = NODE_INDEX;

    vec3 inPosition = getPosition(POSITION_COMPONENT_TYPE, POSITION_NORMALIZED, POSITION_MORPH_TARGET_COUNT);
    gl_Position = camera.projectionViews[pc.viewIndex] * getTransform(SKIN_ATTRIBUTE_COUNT) * vec4(inPosition, 1.0);
    gl_PointSize = 1.0;
}