#ifndef VERTEX_PULLING_GLSL
#define VERTEX_PULLING_GLSL

#include "dequantize.glsl"
#include "indexing.glsl"
#include "types.glsl"

layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer Uint8Ref { uint8_t data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec2Ref { u8vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec3Ref { u8vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec4Ref { u8vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer Uint16Ref { uint16_t data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec2Ref { u16vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec3Ref { u16vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec4Ref { u16vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer FloatRef { float data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec3Ref { vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec4Ref { vec4 data; };

vec3 getPosition(uint morphTargetWeightCount) {
    uint64_t fetchAddress = PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * uint(gl_VertexIndex);
    vec3 position = Vec3Ref(fetchAddress).data;

    for (uint i = 0; i < morphTargetWeightCount; i++) {
        Accessor accessor = PRIMITIVE.positionMorphTargetAccessors.data[i];
        fetchAddress = getFetchAddress(accessor, gl_VertexIndex);
        position += Vec3Ref(fetchAddress).data * NODE.morphTargetWeights.data[i];
    }

    return position;
}

vec3 getNormal(uint morphTargetWeightCount) {
    uint64_t fetchAddress = PRIMITIVE.pNormalBuffer + uint(PRIMITIVE.normalByteStride) * uint(gl_VertexIndex);
    vec3 normal = Vec3Ref(fetchAddress).data;

    for (uint i = 0; i < morphTargetWeightCount; i++) {
        Accessor accessor = PRIMITIVE.normalMorphTargetAccessors.data[i];
        fetchAddress = getFetchAddress(accessor, gl_VertexIndex);
        normal += Vec3Ref(fetchAddress).data * NODE.morphTargetWeights.data[i];
    }

    return normal;
}

vec4 getTangent(uint morphTargetWeightCount) {
    uint64_t fetchAddress = PRIMITIVE.pTangentBuffer + uint(PRIMITIVE.tangentByteStride) * uint(gl_VertexIndex);
    vec4 tangent = Vec4Ref(fetchAddress).data;

    for (uint i = 0; i < morphTargetWeightCount; i++) {
        Accessor accessor = PRIMITIVE.tangentMorphTargetAccessors.data[i];
        fetchAddress = getFetchAddress(accessor, gl_VertexIndex);

        // Tangent morph target only adds XYZ vertex tangent displacements.
        tangent.xyz += Vec3Ref(fetchAddress).data * NODE.morphTargetWeights.data[i];
    }

    return tangent;
}

#if TEXCOORD_COUNT >= 1
vec2 getTexcoord(uint texcoordIndex){
    Accessor texcoordAccessor = PRIMITIVE.texcoordAccessors.data[texcoordIndex];
    uint64_t fetchAddress = getFetchAddress(texcoordAccessor, gl_VertexIndex);

    switch ((PACKED_TEXCOORD_COMPONENT_TYPES >> (8U * texcoordIndex)) & 0xFFU) {
    case 1U: // UNSIGNED BYTE
        return dequantize(U8Vec2Ref(fetchAddress).data);
    case 3U: // UNSIGNED SHORT
        return dequantize(U16Vec2Ref(fetchAddress).data);
    case 6U: // FLOAT
        return Vec2Ref(fetchAddress).data;
    }
    return vec2(0.0); // unreachable.
}
#endif

#if HAS_BASE_COLOR_TEXTURE
vec2 getTexcoord(uint texcoordIndex){
    Accessor texcoordAccessor = PRIMITIVE.texcoordAccessors.data[texcoordIndex];
    uint64_t fetchAddress = getFetchAddress(texcoordAccessor, gl_VertexIndex);

    switch (TEXCOORD_COMPONENT_TYPE) {
    case 1U: // UNSIGNED BYTE
        return dequantize(U8Vec2Ref(fetchAddress).data);
    case 3U: // UNSIGNED SHORT
        return dequantize(U16Vec2Ref(fetchAddress).data);
    case 6U: // FLOAT
        return Vec2Ref(fetchAddress).data;
    }
    return vec2(0.0); // unreachable.
}
#endif

#if HAS_COLOR_ATTRIBUTE
vec4 getColor() {
    uint64_t fetchAddress = PRIMITIVE.pColorBuffer + uint(PRIMITIVE.colorByteStride) * uint(gl_VertexIndex);
    if (COLOR_COMPONENT_COUNT == 3U) {
        switch (COLOR_COMPONENT_TYPE) {
        case 1U: // UNSIGNED BYTE
            return vec4(dequantize(U8Vec3Ref(fetchAddress).data), 1.0);
        case 3U: // UNSIGNED SHORT
            return vec4(dequantize(U16Vec3Ref(fetchAddress).data), 1.0);
        case 6U: // FLOAT
            return vec4(Vec3Ref(fetchAddress).data, 1.0);
        }
    }
    else if (COLOR_COMPONENT_COUNT == 4U) {
        switch (COLOR_COMPONENT_TYPE) {
        case 1U: // UNSIGNED BYTE
            return dequantize(U8Vec4Ref(fetchAddress).data);
        case 3U: // UNSIGNED SHORT
            return dequantize(U16Vec4Ref(fetchAddress).data);
        case 6U: // FLOAT
            return Vec4Ref(fetchAddress).data;
        }
    }
    return vec4(1.0); // unreachable.
}
#endif

#if HAS_COLOR_ALPHA_ATTRIBUTE
float getColorAlpha() {
    // Here uint64_t address should not be used because adding the size of RGB components to it will make 64-bit
    // integer arithmetic instruction.
    uint fetchIndex = uint(PRIMITIVE.colorByteStride) * uint(gl_VertexIndex);
    switch (COLOR_COMPONENT_TYPE) {
    case 1U: // UNSIGNED BYTE
        fetchIndex += 3U; // sizeof(u8vec3)
        return dequantize(Uint8Ref(PRIMITIVE.pColorBuffer + fetchIndex).data);
    case 3U: // UNSIGNED SHORT
        fetchIndex += 6U; // sizeof(u16vec3)
        return dequantize(Uint16Ref(PRIMITIVE.pColorBuffer + fetchIndex).data);
    case 6U: // FLOAT
        fetchIndex += 12; // sizeof(vec3)
        return FloatRef(PRIMITIVE.pColorBuffer + fetchIndex).data;
    }
    return 1.0; // unreachable.
}
#endif

#endif // VERTEX_PULLING_GLSL