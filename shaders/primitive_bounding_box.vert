#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#define VERTEX_SHADER
#include "indexing.glsl"
#include "types.glsl"

layout (set = 0, binding = 0, std430) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 0, binding = 1, std430) readonly buffer NodeBuffer {
    Node nodes[];
};
layout (set = 0, binding = 2) readonly buffer MorphTargetWeightBuffer {
    float morphTargetWeights[];
};
layout (set = 0, binding = 3, std430) readonly buffer SkinJointIndexBuffer {
    uint skinJointIndices[];
};
layout (set = 0, binding = 4) readonly buffer InverseBindMatrixBuffer {
    mat4 inverseBindMatrices[];
};

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
    vec4 color;
    float enlarge;
} pc;

#include "transform.glsl"

void main() {
    vec3 displacement = PRIMITIVE.max - PRIMITIVE.min;
    vec3 min = pc.enlarge * -displacement + PRIMITIVE.max;
    vec3 max = pc.enlarge * displacement + PRIMITIVE.min;
    vec3 position = vec3(((gl_VertexIndex & 4U) == 4U) ? max.x : min.x,
                         ((gl_VertexIndex & 2U) == 2U) ? max.y : min.y,
                         ((gl_VertexIndex & 1U) == 1U) ? max.z : min.z);

    gl_Position = pc.projectionView * getTransform(0U) * vec4(position, 1.0);
}