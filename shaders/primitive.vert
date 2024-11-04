#version 460
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

layout (location = 0) out vec3 outPosition;
layout (location = 1) out mat3 outTBN;
layout (location = 4) out vec2 outBaseColorTexcoord;
layout (location = 5) out vec2 outMetallicRoughnessTexcoord;
layout (location = 6) out vec2 outNormalTexcoord;
layout (location = 7) out vec2 outOcclusionTexcoord;
layout (location = 8) out vec2 outEmissiveTexcoord;
layout (location = 9) flat out uint outMaterialIndex;

layout (set = 1, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (set = 2, binding = 0, std430) readonly buffer NodeBuffer {
    Node nodes[];
};

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
    vec3 viewPosition;
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

vec4 getVec4(uint64_t address){
    return Vec4Ref(address).data;
}

vec2 getTexcoord(uint texcoordIndex){
    IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.texcoordAttributeMappingInfos.data[texcoordIndex];
    return getVec2(mappingInfo.bytesPtr + uint(mappingInfo.stride) * gl_VertexIndex);
}

void main(){
    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    vec3 inNormal = getVec3(PRIMITIVE.pNormalBuffer + uint(PRIMITIVE.normalByteStride) * gl_VertexIndex);

    mat4 transform = TRANSFORM;
    outPosition = (transform * vec4(inPosition, 1.0)).xyz;
    outTBN[2] = normalize(mat3(transform) * inNormal); // N

    if (int(MATERIAL.baseColorTextureIndex) != -1){
        outBaseColorTexcoord = getTexcoord(uint(MATERIAL.baseColorTexcoordIndex));
    }
    if (int(MATERIAL.metallicRoughnessTextureIndex) != -1){
        outMetallicRoughnessTexcoord = getTexcoord(uint(MATERIAL.metallicRoughnessTexcoordIndex));
    }
    if (int(MATERIAL.normalTextureIndex) != -1){
        vec4 inTangent = getVec4(PRIMITIVE.pTangentBuffer + uint(PRIMITIVE.tangentByteStride) * gl_VertexIndex);
        outTBN[0] = normalize(mat3(transform) * inTangent.xyz); // T
        outTBN[1] = cross(outTBN[2], outTBN[0]) * -inTangent.w; // B

        outNormalTexcoord = getTexcoord(uint(MATERIAL.normalTexcoordIndex));
    }
    if (int(MATERIAL.occlusionTextureIndex) != -1){
        outOcclusionTexcoord = getTexcoord(uint(MATERIAL.occlusionTexcoordIndex));
    }
    if (int(MATERIAL.emissiveTextureIndex) != -1){
        outEmissiveTexcoord = getTexcoord(uint(MATERIAL.emissiveTexcoordIndex));
    }
    outMaterialIndex = MATERIAL_INDEX;

    gl_Position = pc.projectionView * vec4(outPosition, 1.0);
}