#ifndef TYPES_GLSL
#define TYPES_GLSL

struct Material {
    uint8_t baseColorTexcoordIndex;
    uint8_t metallicRoughnessTexcoordIndex;
    uint8_t normalTexcoordIndex;
    uint8_t occlusionTexcoordIndex;
    uint8_t emissiveTexcoordIndex;
    uint8_t padding0[1];
    uint16_t baseColorTextureIndex;
    uint16_t metallicRoughnessTextureIndex;
    uint16_t normalTextureIndex;
    uint16_t occlusionTextureIndex;
    uint16_t emissiveTextureIndex;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    vec3 emissiveFactor;
    float alphaCutoff;
    mat3x2 baseColorTextureTransform;
    mat3x2 metallicRoughnessTextureTransform;
    mat3x2 normalTextureTransform;
    mat3x2 occlusionTextureTransform;
    mat3x2 emissiveTextureTransform;
    vec2 padding1;
}; // 192 bytes.

// --------------------
// Vertex shader only types
// --------------------

#ifdef VERTEX_SHADER

struct Node {
    uint instancedTransformStartIndex;
    uint morphTargetWeightStartIndex;
};

struct Accessor {
    uint64_t bufferAddress;
    uint8_t componentType;
    uint8_t componentCount;
    uint8_t stride;
    uint8_t _padding_[5];
};

uint64_t getFetchAddress(Accessor accessor, uint index) {
    return accessor.bufferAddress + uint(accessor.stride) * index;
}

layout (std430, buffer_reference, buffer_reference_align = 16) readonly buffer Accessors { Accessor data[]; };

struct Primitive {
    uint64_t pPositionBuffer;
    Accessors positionMorphTargetAccessors;
    uint64_t pNormalBuffer;
    Accessors normalMorphTargetAccessors;
    uint64_t pTangentBuffer;
    Accessors tangentMorphTargetAccessors;
    uint64_t pColorBuffer;
    Accessors texcoordAccessors;
    Accessors jointsAccessors;
    Accessors weightsAccessors;
    uint8_t positionByteStride;
    uint8_t normalByteStride;
    uint8_t tangentByteStride;
    uint8_t colorByteStride;
    uint materialIndex;
    vec2 _padding0_;
};

#endif

#endif // TYPES_GLSL