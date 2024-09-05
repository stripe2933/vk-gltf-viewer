#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// For convinience.
#define PRIMITIVE primitives[primitiveIndex]
#define MATERIAL materials[PRIMITIVE.materialIndex + 1]

struct Material {
    uint8_t baseColorTexcoordIndex;
    uint8_t metallicRoughnessTexcoordIndex;
    uint8_t normalTexcoordIndex;
    uint8_t occlusionTexcoordIndex;
    uint8_t emissiveTexcoordIndex;
    uint8_t padding0[1];
    int16_t baseColorTextureIndex;
    int16_t metallicRoughnessTextureIndex;
    int16_t normalTextureIndex;
    int16_t occlusionTextureIndex;
    int16_t emissiveTextureIndex;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    vec3 emissiveFactor;
    float alphaCutoff;
};

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

layout (location = 0) in vec2 fragBaseColorTexcoord;
layout (location = 1) flat in uint primitiveIndex;

layout (location = 0) out uint outNodeIndex;

layout (set = 0, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};

layout (set = 1, binding = 0) uniform sampler2D textures[];
layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

void main(){
    float baseColorAlpha = MATERIAL.baseColorFactor.a * texture(textures[uint(MATERIAL.baseColorTexcoordIndex) + 1], fragBaseColorTexcoord).a;
    if (baseColorAlpha < MATERIAL.alphaCutoff) discard;

    outNodeIndex = PRIMITIVE.nodeIndex;
}