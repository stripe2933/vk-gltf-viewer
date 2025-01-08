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
    mat2 baseColorTextureTransformUpperLeft2x2;
    vec2 baseColorTextureTransformOffset;
    mat2 metallicRoughnessTextureTransformUpperLeft2x2;
    vec2 metallicRoughnessTextureTransformOffset;
    mat2 normalTextureTransformUpperLeft2x2;
    vec2 normalTextureTransformOffset;
    mat2 occlusionTextureTransformUpperLeft2x2;
    vec2 occlusionTextureTransformOffset;
    mat2 emissiveTextureTransformUpperLeft2x2;
    vec2 emissiveTextureTransformOffset;
    vec2 padding1;
}; // 192 bytes.

// --------------------
// Vertex shader only types
// --------------------

#ifdef VERTEX_SHADER

struct IndexedAttributeMappingInfo {
    uint64_t bytesPtr;
    uint8_t stride;
};

layout (std430, buffer_reference, buffer_reference_align = 16) readonly buffer IndexedAttributeMappingInfos { IndexedAttributeMappingInfo data[]; };

struct Primitive {
    uint64_t pPositionBuffer;
    uint64_t pNormalBuffer;
    uint64_t pTangentBuffer;
    IndexedAttributeMappingInfos texcoordAttributeMappingInfos;
    uint64_t pColorBuffer;
    uint8_t positionByteStride;
    uint8_t normalByteStride;
    uint8_t tangentByteStride;
    uint8_t colorByteStride;
    uint materialIndex;
};

#endif