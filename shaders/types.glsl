#ifndef TYPES_GLSL
#define TYPES_GLSL

struct Material {
    uint baseColorPackedTextureInfo;
    uint metallicRoughnessPackedTextureInfo;
    uint normalPackedTextureInfo;
    uint occlusionPackedTextureInfo;
    uint emissivePackedTextureInfo;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    float occlusionStrength;
    float alphaCutoff;
    float _padding_;
    mat3x2 baseColorTextureTransform;
    mat3x2 metallicRoughnessTextureTransform;
    mat3x2 normalTextureTransform;
    mat3x2 occlusionTextureTransform;
    mat3x2 emissiveTextureTransform;
}; // 192 bytes.

uint getTexcoordIndex(uint packedTextureInfo) {
    return packedTextureInfo & 3U;
}

uint getTextureIndex(uint packedTextureInfo) {
    return packedTextureInfo >> 2U;
}

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
    Accessors texcoordAccessors;
    uint64_t pColorBuffer;
    uint8_t positionByteStride;
    uint8_t normalByteStride;
    uint8_t tangentByteStride;
    uint8_t colorByteStride;
    uint materialIndex;
    vec2 _padding0_;
};

#endif

#endif // TYPES_GLSL