#version 460
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

// For convinience.
#define PRIMITIVE primitives[gl_BaseInstance]
#define TRANSFORM nodeTransforms[PRIMITIVE.nodeIndex]

layout (std430, buffer_reference, buffer_reference_align = 8) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 16) readonly buffer Vec4Ref { vec4 data; };

struct Primitive {
    uint64_t pPositionBuffer;
    uint64_t pNormalBuffer;
    uint64_t pTangentBuffer;
    uint64_t texcoordAttributeMappingInfos;
    uint64_t colorAttributeMappingInfos;
    uint8_t positionByteStride;
    uint8_t normalByteStride;
    uint8_t tangentByteStride;
    uint8_t padding;
    uint nodeIndex;
    int materialIndex;
};

layout (set = 0, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 0, binding = 1) readonly buffer NodeTransformBuffer {
    mat4 nodeTransforms[];
};

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
} pc;

// --------------------
// Functions.
// --------------------

vec3 getVec3(uint64_t address){
    return Vec4Ref(address).data.xyz;
}

void main(){
    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    gl_Position = pc.projectionView * TRANSFORM * vec4(inPosition, 1.0);
}