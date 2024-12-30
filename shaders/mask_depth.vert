#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

#define VERTEX_SHADER
#include "indexing.glsl"
#include "types.glsl"

layout (std430, buffer_reference, buffer_reference_align = 8) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 16) readonly buffer Vec4Ref { vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 64) readonly buffer Node { mat4 transforms[]; };

layout (location = 0) out vec2 outBaseColorTexcoord;
layout (location = 1) flat out uint outNodeIndex;
layout (location = 2) flat out uint outMaterialIndex;

layout (set = 0, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 0, binding = 1, std430) readonly buffer MaterialBuffer {
    Material materials[];
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

vec2 getVec2(uint64_t address){
    return Vec2Ref(address).data;
}

vec3 getVec3(uint64_t address){
    return Vec4Ref(address).data.xyz;
}

vec2 getTexcoord(uint texcoordIndex){
    IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.texcoordAttributeMappingInfos.data[texcoordIndex];
    return getVec2(mappingInfo.bytesPtr + uint(mappingInfo.stride) * gl_VertexIndex);
}

void main(){
    if (int(MATERIAL.baseColorTextureIndex) != -1){
        outBaseColorTexcoord = getTexcoord(uint(MATERIAL.baseColorTexcoordIndex));
    }
    outNodeIndex = NODE_INDEX;
    outMaterialIndex = MATERIAL_INDEX;

    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    gl_Position = pc.projectionView * TRANSFORM * vec4(inPosition, 1.0);
}