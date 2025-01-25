#ifndef VERTEX_PULLING_GLSL
#define VERTEX_PULLING_GLSL

#define VERTEX_SHADER
#include "branch.glsl"
#include "dequantization.glsl"
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

vec3 getPosition() {
    vec3 position = Vec3Ref(PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * uint(gl_VertexIndex)).data;
    // Morph target computation.
    if (uint64_t(PRIMITIVE.positionMorphTargetAttributeMappingInfos) != 0) {
        for (uint i = 0U; i < PRIMITIVE.morphTargetCount; ++i) {
            IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.positionMorphTargetAttributeMappingInfos.data[i];
            position += /* TODO: weight */ Vec3Ref(mappingInfo.bytesPtr + mappingInfo.stride * uint(gl_VertexIndex)).data;
        }
    }
    return position;
}

vec3 getNormal() {
    vec3 normal = Vec3Ref(PRIMITIVE.pNormalBuffer + uint(PRIMITIVE.normalByteStride) * uint(gl_VertexIndex)).data;
    // Morph target computation.
    if (uint64_t(PRIMITIVE.normalMorphTargetAttributeMappingInfos) != 0) {
        for (uint i = 0U; i < PRIMITIVE.morphTargetCount; ++i) {
            IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.normalMorphTargetAttributeMappingInfos.data[i];
            normal += /* TODO: weight */ Vec3Ref(mappingInfo.bytesPtr + mappingInfo.stride * uint(gl_VertexIndex)).data;
        }
    }
    return normal;
}

vec4 getTangent() {
    vec4 tangent = Vec4Ref(PRIMITIVE.pTangentBuffer + uint(PRIMITIVE.tangentByteStride) * uint(gl_VertexIndex)).data;
    // Morph target computation.
    if (uint64_t(PRIMITIVE.tangentMorphTargetAttributeMappingInfos) != 0) {
        for (uint i = 0U; i < PRIMITIVE.morphTargetCount; ++i) {
            IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.tangentMorphTargetAttributeMappingInfos.data[i];
            tangent.xyz += /* TODO: weight */ Vec3Ref(mappingInfo.bytesPtr + mappingInfo.stride * uint(gl_VertexIndex)).data;
        }
    }
    return tangent;
}

vec2 getTexcoord(uint texcoordIndex){
    IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.texcoordAttributeMappingInfos.data[texcoordIndex];
    uint64_t fetchAddress = mappingInfo.bytesPtr + mappingInfo.stride * uint(gl_VertexIndex);

    if (mappingInfo.componentType == uint8_t(6)) { // 5126: FLOAT
        return Vec2Ref(fetchAddress).data;
    }
    if (mappingInfo.componentType == uint8_t(3)) { // 5123: UNSIGNED SHORT
        return dequantize(U16Vec2Ref(fetchAddress).data);
    }
    if last_branch(mappingInfo.componentType == uint8_t(1)) { // 5121: UNSIGNED BYTE
        return dequantize(U8Vec2Ref(fetchAddress).data);
    }
}

vec4 getColor() {
    uint64_t fetchAddress = PRIMITIVE.pColorBuffer + uint(PRIMITIVE.colorByteStride) * uint(gl_VertexIndex);
    if (PRIMITIVE.colorComponentCount == uint8_t(4)) {
        if (PRIMITIVE.colorComponentType == uint8_t(6)) { // 5126: FLOAT
            return Vec4Ref(fetchAddress).data;
        }
        if (PRIMITIVE.colorComponentType == uint8_t(3)) { // 5123: UNSIGNED SHORT
            return dequantize(U16Vec4Ref(fetchAddress).data);
        }
        if last_branch(PRIMITIVE.colorComponentType == uint8_t(1)) { // 5121: UNSIGNED BYTE
            return dequantize(U8Vec4Ref(fetchAddress).data);
        }
    }
    if last_branch(PRIMITIVE.colorComponentCount == uint8_t(3)) {
        if (PRIMITIVE.colorComponentType == uint8_t(6)) { // 5126: FLOAT
            return vec4(Vec3Ref(fetchAddress).data, 1.0);
        }
        if (PRIMITIVE.colorComponentType == uint8_t(3)) { // 5123: UNSIGNED SHORT
            return vec4(dequantize(U16Vec3Ref(fetchAddress).data), 1.0);
        }
        if last_branch(PRIMITIVE.colorComponentType == uint8_t(1)) { // 5121: UNSIGNED BYTE
            return vec4(dequantize(U8Vec3Ref(fetchAddress).data), 1.0);
        }
    }
}

float getColorAlpha() {
    uint fetchByteOffset = uint(PRIMITIVE.colorByteStride) * uint(gl_VertexIndex);
    if (PRIMITIVE.colorComponentType == uint8_t(6)) { // 5126: FLOAT
        return FloatRef(PRIMITIVE.pColorBuffer + (fetchByteOffset + 12)).data;
    }
    if (PRIMITIVE.colorComponentType == uint8_t(3)) { // 5123: UNSIGNED SHORT
        return float(Uint16Ref(PRIMITIVE.pColorBuffer + (fetchByteOffset + 6)).data) / 65535.0;
    }
    if last_branch(PRIMITIVE.colorComponentType == uint8_t(1)) { // 5121: UNSIGNED BYTE
        return float(Uint8Ref(PRIMITIVE.pColorBuffer + (fetchByteOffset + 3)).data) / 255.0;
    }
}

#endif