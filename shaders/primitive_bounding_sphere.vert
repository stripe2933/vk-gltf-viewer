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

const vec3 ICOSPHERE[] = {
    { 0.0000, 0.0000, 1.0000 },
    { 0.8944, 0.0000, 0.4472 },
    { 0.2764, 0.8507, 0.4472 },
    { -0.7236, 0.5257, 0.4472 },
    { -0.7236, -0.5257, 0.4472 },
    { 0.2764, -0.8507, 0.4472 },
    { 0.7236, 0.5257, -0.4472 },
    { -0.2764, 0.8507, -0.4472 },
    { -0.8944, 0.0000, -0.4472 },
    { -0.2764, -0.8507, -0.4472 },
    { 0.7236, -0.5257, -0.4472 },
    { 0.0000, 0.0000, -1.0000 },
    { 0.5257, 0.0000, 0.8507 },
    { 0.6882, 0.5000, 0.5257 },
    { 0.1625, 0.5000, 0.8507 },
    { -0.2629, 0.8090, 0.5257 },
    { -0.4253, 0.3090, 0.8507 },
    { -0.8507, 0.0000, 0.5257 },
    { -0.4253, -0.3090, 0.8507 },
    { -0.2629, -0.8090, 0.5257 },
    { 0.1625, -0.5000, 0.8507 },
    { 0.6882, -0.5000, 0.5257 },
    { 0.9511, 0.3090, 0.0000 },
    { 0.5878, 0.8090, 0.0000 },
    { 0.0000, 1.0000, 0.0000 },
    { -0.5878, 0.8090, 0.0000 },
    { -0.9511, 0.3090, 0.0000 },
    { -0.9511, -0.3090, 0.0000 },
    { -0.5878, -0.8090, 0.0000 },
    { 0.0000, -1.0000, 0.0000 },
    { 0.5878, -0.8090, 0.0000 },
    { 0.9511, -0.3090, 0.0000 },
    { 0.2629, 0.8090, -0.5257 },
    { -0.6882, 0.5000, -0.5257 },
    { -0.6882, -0.5000, -0.5257 },
    { 0.2629, -0.8090, -0.5257 },
    { 0.8507, 0.0000, -0.5257 },
    { 0.4253, 0.3090, -0.8507 },
    { -0.1625, 0.5000, -0.8507 },
    { -0.5257, 0.0000, -0.8507 },
    { -0.1625, -0.5000, -0.8507 },
    { 0.4253, -0.3090, -0.8507 },
};

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
#if 1
    vec3 min = PRIMITIVE.min;
    vec3 max = PRIMITIVE.max;
    vec3 halfDisplacement = 0.5 * (max - min);
    vec3 center = min + halfDisplacement;
    float radius = pc.enlarge * length(halfDisplacement);

    gl_Position = pc.projectionView * getTransform(0U) * vec4(radius * ICOSPHERE[gl_VertexIndex] + center, 1.0);
#else
    mat4 transform = getTransform(0U);
    vec3 min = (transform * vec4(PRIMITIVE.min, 1.0)).xyz;
    vec3 max = (transform * vec4(PRIMITIVE.max, 1.0)).xyz;
    vec3 halfDisplacement = 0.5 * (max - min);
    vec3 center = min + halfDisplacement;
    float radius = pc.enlarge * length(halfDisplacement);

    gl_Position = pc.projectionView * vec4(radius * ICOSPHERE[gl_VertexIndex] + center, 1.0);
#endif
}