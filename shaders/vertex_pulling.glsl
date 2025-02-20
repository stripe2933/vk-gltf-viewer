#ifndef VERTEX_PULLING_GLSL
#define VERTEX_PULLING_GLSL

#include "dequantize.glsl"
#include "indexing.glsl"
#include "types.glsl"

layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer Int8Ref { int8_t data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer Uint8Ref { uint8_t data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer I8Vec2Ref { i8vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec2Ref { u8vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer I8Vec3Ref { i8vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec3Ref { u8vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer I8Vec4Ref { i8vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec4Ref { u8vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer Int16Ref { int16_t data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer Uint16Ref { uint16_t data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer I16Vec2Ref { i16vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec2Ref { u16vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer I16Vec3Ref { i16vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec3Ref { u16vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer I16Vec4Ref { i16vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec4Ref { u16vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer FloatRef { float data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec3Ref { vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec4Ref { vec4 data; };

vec3 getPosition(uint componentType, uint morphTargetWeightCount) {
    uint64_t fetchAddress = PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * uint(gl_VertexIndex);
    vec3 position;
    switch (componentType) {
    case 0U: // BYTE
        position = vec3(I8Vec3Ref(fetchAddress).data);
        break;
    case 1U: // UNSIGNED BYTE
        position = vec3(U8Vec3Ref(fetchAddress).data);
        break;
    case 2U: // SHORT
        position = vec3(I16Vec3Ref(fetchAddress).data);
        break;
    case 3U: // UNSIGNED SHORT
        position = vec3(U16Vec3Ref(fetchAddress).data);
        break;
    case 6U: // FLOAT
        position = Vec3Ref(fetchAddress).data;
        break;
    case 8U: // BYTE normalized
        position = dequantize(I8Vec3Ref(fetchAddress).data);
        break;
    case 9U: // UNSIGNED BYTE normalized
        position = dequantize(U8Vec3Ref(fetchAddress).data);
        break;
    case 10U: // SHORT normalized
        position = dequantize(I16Vec3Ref(fetchAddress).data);
        break;
    case 11U: // UNSIGNED SHORT normalized
        position = dequantize(U16Vec3Ref(fetchAddress).data);
        break;
    }

    for (uint i = 0; i < morphTargetWeightCount; i++) {
        Accessor accessor = PRIMITIVE.positionMorphTargetAccessors.data[i];
        fetchAddress = getFetchAddress(accessor, gl_VertexIndex);

        float weight = morphTargetWeights[NODE.morphTargetWeightStartIndex + i];
        switch (uint(accessor.componentType)) {
        case 0U: // BYTE
            position += weight * vec3(I8Vec3Ref(fetchAddress).data);
            break;
        case 2U: // SHORT
            position += weight * vec3(I16Vec3Ref(fetchAddress).data);
            break;
        case 6U:
            position += weight * Vec3Ref(fetchAddress).data;
            break;
        case 8U: // BYTE normalized
            position += weight * dequantize(I8Vec3Ref(fetchAddress).data);
            break;
        case 10U: // SHORT normalized
            position += weight * dequantize(I16Vec3Ref(fetchAddress).data);
            break;
        }
    }

    return position;
}

vec3 getNormal(uint componentType, uint morphTargetWeightCount) {
    uint64_t fetchAddress = PRIMITIVE.pNormalBuffer + uint(PRIMITIVE.normalByteStride) * uint(gl_VertexIndex);
    vec3 normal;
    switch (componentType) {
    case 6U: // FLOAT
        normal = Vec3Ref(fetchAddress).data;
        break;
    case 8U: // BYTE normalized
        normal = dequantize(I8Vec3Ref(fetchAddress).data);
        break;
    case 10U: // SHORT normalized
        normal = dequantize(I16Vec3Ref(fetchAddress).data);
        break;
    }

    for (uint i = 0; i < morphTargetWeightCount; i++) {
        Accessor accessor = PRIMITIVE.normalMorphTargetAccessors.data[i];
        fetchAddress = getFetchAddress(accessor, gl_VertexIndex);

        float weight = morphTargetWeights[NODE.morphTargetWeightStartIndex + i];
        switch (uint(accessor.componentType)) {
        case 6U: // FLOAT
            normal += weight * Vec3Ref(fetchAddress).data;
            break;
        case 8U: // BYTE normalized
            normal += weight * dequantize(I8Vec3Ref(fetchAddress).data);
            break;
        case 10U: // SHORT normalized
            normal += weight * dequantize(I16Vec3Ref(fetchAddress).data);
            break;
        }
    }

    return normal;
}

vec4 getTangent(uint componentType, uint morphTargetWeightCount) {
    uint64_t fetchAddress = PRIMITIVE.pTangentBuffer + uint(PRIMITIVE.tangentByteStride) * uint(gl_VertexIndex);
    vec4 tangent;
    switch (componentType) {
    case 6U: // FLOAT
        tangent = Vec4Ref(fetchAddress).data;
        break;
    case 8U: // BYTE normalized
        tangent = dequantize(I8Vec4Ref(fetchAddress).data);
        break;
    case 10U: // SHORT normalized
        tangent = dequantize(I16Vec4Ref(fetchAddress).data);
        break;
    }

    for (uint i = 0; i < morphTargetWeightCount; i++) {
        Accessor accessor = PRIMITIVE.tangentMorphTargetAccessors.data[i];
        fetchAddress = getFetchAddress(accessor, gl_VertexIndex);

        // Tangent morph target only adds XYZ vertex tangent displacements.
        float weight = morphTargetWeights[NODE.morphTargetWeightStartIndex + i];
        switch (uint(accessor.componentType)) {
        case 6U: // FLOAT
            tangent.xyz += weight * Vec3Ref(fetchAddress).data;
            break;
        case 8U: // BYTE normalized
            tangent.xyz += weight * dequantize(I8Vec3Ref(fetchAddress).data);
            break;
        case 10U: // SHORT normalized
            tangent.xyz += weight * dequantize(I16Vec3Ref(fetchAddress).data);
            break;
        }
    }

    return tangent;
}

#if TEXCOORD_COUNT >= 1 || HAS_BASE_COLOR_TEXTURE
vec2 getTexcoord(uint texcoordIndex, uint componentType){
    Accessor texcoordAccessor = PRIMITIVE.texcoordAccessors.data[texcoordIndex];
    uint64_t fetchAddress = getFetchAddress(texcoordAccessor, gl_VertexIndex);

    switch (componentType) {
    case 0U: // BYTE
        return vec2(I8Vec2Ref(fetchAddress).data);
    case 1U: // UNSIGNED BYTE
        return vec2(U8Vec2Ref(fetchAddress).data);
    case 2U: // SHORT
        return vec2(I16Vec2Ref(fetchAddress).data);
    case 3U: // UNSIGNED SHORT
        return vec2(U16Vec2Ref(fetchAddress).data);
    case 6U: // FLOAT
        return Vec2Ref(fetchAddress).data;
    case 8U: // BYTE normalized
        return dequantize(I8Vec2Ref(fetchAddress).data);
    case 9U: // UNSIGNED BYTE normalized
        return dequantize(U8Vec2Ref(fetchAddress).data);
    case 10U: // SHORT normalized
        return dequantize(I16Vec2Ref(fetchAddress).data);
    case 11U: // UNSIGNED SHORT normalized
        return dequantize(U16Vec2Ref(fetchAddress).data);
    }
    return vec2(0.0); // unreachable.
}
#endif

#if HAS_COLOR_ATTRIBUTE
vec4 getColor(uint componentType) {
    uint64_t fetchAddress = PRIMITIVE.pColorBuffer + uint(PRIMITIVE.colorByteStride) * uint(gl_VertexIndex);
    if (COLOR_COMPONENT_COUNT == 3U) {
        switch (componentType) {
        case 6U: // FLOAT
            return vec4(Vec3Ref(fetchAddress).data, 1.0);
        case 9U: // UNSIGNED BYTE normalized
            return vec4(dequantize(U8Vec3Ref(fetchAddress).data), 1.0);
        case 11U: // UNSIGNED SHORT normalized
            return vec4(dequantize(U16Vec3Ref(fetchAddress).data), 1.0);
        }
    }
    else if (COLOR_COMPONENT_COUNT == 4U) {
        switch (componentType) {
        case 6U: // FLOAT
            return Vec4Ref(fetchAddress).data;
        case 9U: // UNSIGNED BYTE normalized
            return dequantize(U8Vec4Ref(fetchAddress).data);
        case 11U: // UNSIGNED SHORT normalized
            return dequantize(U16Vec4Ref(fetchAddress).data);
        }
    }
    return vec4(1.0); // unreachable.
}
#endif

#if HAS_COLOR_ALPHA_ATTRIBUTE
float getColorAlpha(uint componentType) {
    // Here uint64_t address should not be used because adding the size of RGB components to it will make 64-bit
    // integer arithmetic instruction.
    uint fetchIndex = uint(PRIMITIVE.colorByteStride) * uint(gl_VertexIndex);
    switch (componentType) {
    case 6U: // FLOAT
        fetchIndex += 12; // sizeof(vec3)
        return FloatRef(PRIMITIVE.pColorBuffer + fetchIndex).data;
    case 9U: // UNSIGNED BYTE normalized
        fetchIndex += 3U; // sizeof(u8vec3)
        return dequantize(Uint8Ref(PRIMITIVE.pColorBuffer + fetchIndex).data);
    case 11U: // UNSIGNED SHORT normalized
        fetchIndex += 6U; // sizeof(u16vec3)
        return dequantize(Uint16Ref(PRIMITIVE.pColorBuffer + fetchIndex).data);
    }
    return 1.0; // unreachable.
}
#endif

#endif // VERTEX_PULLING_GLSL