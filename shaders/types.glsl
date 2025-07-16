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
    vec3 emissive;
    float alphaCutoff;
    mat3x2 baseColorTextureTransform;
    mat3x2 metallicRoughnessTextureTransform;
    mat3x2 normalTextureTransform;
    mat3x2 occlusionTextureTransform;
    mat3x2 emissiveTextureTransform;
    float ior;
    float padding1;
}; // 192 bytes.

// --------------------
// Vertex shader only types
// --------------------

#ifdef VERTEX_SHADER

layout (buffer_reference) readonly buffer Matrices { mat4 data[]; };
layout (buffer_reference, buffer_reference_align = 4) readonly buffer MorphTargetWeights { float data[]; };
layout (buffer_reference, buffer_reference_align = 4) readonly buffer SkinJointIndices { uint data[]; };

struct Node {
    mat4 worldTransform;
    Matrices instancedWorldTransforms;
    MorphTargetWeights morphTargetWeights;
    SkinJointIndices skinJointIndices;
    Matrices inverseBindMatrices;
};

struct Accessor {
    uvec2 bufferAddress;
    uint componentType;
    uint stride;
};

uvec2 add64(uvec2 lhs, uint rhs) {
    uint carry;
    uint lo = uaddCarry(lhs.x, rhs, carry);
    uint hi = lhs.y + carry;
    return uvec2(lo, hi);
}

uvec2 getFetchAddress(Accessor accessor, uint index) {
    return add64(accessor.bufferAddress, accessor.stride * index);
}

layout (std430, buffer_reference, buffer_reference_align = 16) readonly buffer Accessors { Accessor data[]; };

struct Primitive {
    Accessor positionAccessor;
    Accessor normalAccessor;
    Accessor tangentAccessor;
    Accessor texcoordAccessors[4];
    Accessor color0Accessor;
    Accessors positionMorphTargetAccessors;
    Accessors normalMorphTargetAccessors;
    Accessors tangentMorphTargetAccessors;
    Accessors jointAccessors;
    Accessors weightAccessors;
    int materialIndex;
    uint _padding;
};

#endif

#endif // TYPES_GLSL