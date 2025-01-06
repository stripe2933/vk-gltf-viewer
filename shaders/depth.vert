#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

#define VERTEX_SHADER
#include "indexing.glsl"
#include "types.glsl"

layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec3Ref { vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 64) readonly buffer Node { mat4 transforms[]; };

layout (location = 0) flat out uint outNodeIndex;

layout (set = 0, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};

layout (set = 1, binding = 0, std430) readonly buffer NodeBuffer {
    Node nodes[];
};

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
} pc;

// --------------------
// Functions.
// --------------------

vec3 getPosition() {
    return Vec3Ref(PRIMITIVE.pPositionBuffer + int(PRIMITIVE.positionByteStride) * gl_VertexIndex).data;
}

void main(){
    outNodeIndex = NODE_INDEX;

    vec3 inPosition = getPosition();
    gl_Position = pc.projectionView * TRANSFORM * vec4(inPosition, 1.0);
}