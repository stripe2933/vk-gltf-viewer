#ifndef VERTEX_PULLING_GLSL
#define VERTEX_PULLING_GLSL

#include "indexing.glsl"
#include "types.glsl"

layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer I8Vec2Ref { i8vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer U8Vec2Ref { u8vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer I8Vec3Ref { i8vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer U8Vec3Ref { u8vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer U8Vec4Ref { u8vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer I16Vec2Ref { i16vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer U16Vec2Ref { u16vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer I16Vec3Ref { i16vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer U16Vec3Ref { u16vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer U16Vec4Ref { u16vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer FloatRef { float data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec3Ref { vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec4Ref { vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer UIntRef { uint data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer UVec2Ref { uvec2 data; };

vec3 getPosition(uint componentType, bool normalized, uint morphTargetWeightCount) {
    vec3 position;

    uvec2 fetchAddress = add64(PRIMITIVE.positionAccessor.bufferAddress, PRIMITIVE.positionAccessor.stride * uint(gl_VertexIndex));
    switch (componentType) {
    case 5120U: { // BYTE
        position = unpackSnorm4x8(UIntRef(fetchAddress).data).xyz;
        if (!normalized) {
            // map [-1, 1] to [-128, 127]
            position = 127.5 * position - 0.5;
        }
        break;
    }
    case 5121U: { // UNSIGNED BYTE
        position = unpackUnorm4x8(UIntRef(fetchAddress).data).xyz;
        if (!normalized) {
            position *= 255.0;
        }
        break;
    }
    case 5122U: { // SHORT
        uvec2 fetched = UVec2Ref(fetchAddress).data;
        position = vec3(unpackSnorm2x16(fetched.x), unpackSnorm2x16(fetched.y).x);
        if (!normalized) {
            // map [-1, 1] to [-32768, 32767]
            position = 32767.5 * position - 0.5;
        }
        break;
    }
    case 5123U: { // UNSIGNED SHORT
        uvec2 fetched = UVec2Ref(fetchAddress).data;
        position = vec3(unpackUnorm2x16(fetched.x), unpackUnorm2x16(fetched.y).x);
        if (!normalized) {
            position *= 65535.0;
        }
        break;
    }
    case 5126U: // FLOAT
        position = Vec3Ref(fetchAddress).data;
        break;
    }

    for (uint i = 0; i < morphTargetWeightCount; i++) {
        Accessor accessor = PRIMITIVE.positionMorphTargetAccessors.data[i];
        fetchAddress = getFetchAddress(accessor, gl_VertexIndex);

        float weight = NODE.morphTargetWeights.data[i];
        switch (accessor.componentType) {
        case 0U: { // BYTE
            vec3 add = weight * unpackSnorm4x8(UIntRef(fetchAddress).data).xyz;
            // map [-1, 1] to [-128, 127]
            add = 127.5 * add - 0.5;
            position += add;
            break;
        }
        case 2U: { // SHORT
            uvec2 fetched = UVec2Ref(fetchAddress).data;
            vec3 add = weight * vec3(unpackSnorm2x16(fetched.x), unpackSnorm2x16(fetched.y).x);
            // map [-1, 1] to [-32768, 32767]
            add = 32767.5 * add - 0.5;
            position += add;
            break;
        }
        case 6U: // FLOAT
            position += weight * Vec3Ref(fetchAddress).data;
            break;
        case 8U: // BYTE normalized
            position += weight * unpackSnorm4x8(UIntRef(fetchAddress).data).xyz;
            break;
        case 10U: // SHORT normalized
            uvec2 fetched = UVec2Ref(fetchAddress).data;
            position += weight * vec3(unpackSnorm2x16(fetched.x), unpackSnorm2x16(fetched.y).x);
            break;
        }
    }

    return position;
}

vec3 getNormal(uint componentType, uint morphTargetWeightCount) {
    vec3 normal;

    uvec2 fetchAddress = add64(PRIMITIVE.normalAccessor.bufferAddress, PRIMITIVE.normalAccessor.stride * uint(gl_VertexIndex));
    switch (componentType) {
    case 5126U: // FLOAT
        normal = Vec3Ref(fetchAddress).data;
        break;
    case 5120U: // BYTE normalized
        normal = unpackSnorm4x8(UIntRef(fetchAddress).data).xyz;
        break;
    case 5122U: // SHORT normalized
        uvec2 fetched = UVec2Ref(fetchAddress).data;
        normal = vec3(unpackSnorm2x16(fetched.x), unpackSnorm2x16(fetched.y).x);
        break;
    }

    for (uint i = 0; i < morphTargetWeightCount; i++) {
        Accessor accessor = PRIMITIVE.normalMorphTargetAccessors.data[i];
        fetchAddress = getFetchAddress(accessor, gl_VertexIndex);

        float weight = NODE.morphTargetWeights.data[i];
        switch (accessor.componentType) {
        case 6U: // FLOAT
            normal += weight * Vec3Ref(fetchAddress).data;
            break;
        case 8U: // BYTE normalized
            normal += weight * unpackSnorm4x8(UIntRef(fetchAddress).data).xyz;
            break;
        case 10U: // SHORT normalized
            uvec2 fetched = UVec2Ref(fetchAddress).data;
            normal += weight * vec3(unpackSnorm2x16(fetched.x), unpackSnorm2x16(fetched.y).x);
            break;
        }
    }

    return normal;
}

vec4 getTangent(uint componentType, uint morphTargetWeightCount) {
    vec4 tangent;

    uvec2 fetchAddress = add64(PRIMITIVE.tangentAccessor.bufferAddress, PRIMITIVE.tangentAccessor.stride * uint(gl_VertexIndex));
    switch (componentType) {
    case 5126U: // FLOAT
        tangent = Vec4Ref(fetchAddress).data;
        break;
    case 5120U: // BYTE normalized
        tangent = unpackSnorm4x8(UIntRef(fetchAddress).data);
        break;
    case 5122U: // SHORT normalized
        uvec2 fetched = UVec2Ref(fetchAddress).data;
        tangent = vec4(unpackSnorm2x16(fetched.x), unpackSnorm2x16(fetched.y));
        break;
    }

    for (uint i = 0; i < morphTargetWeightCount; i++) {
        Accessor accessor = PRIMITIVE.tangentMorphTargetAccessors.data[i];
        fetchAddress = getFetchAddress(accessor, gl_VertexIndex);

        // Tangent morph target only adds XYZ vertex tangent displacements.
        float weight = NODE.morphTargetWeights.data[i];
        switch (accessor.componentType) {
        case 6U: // FLOAT
            tangent.xyz += weight * Vec3Ref(fetchAddress).data;
            break;
        case 8U: // BYTE normalized
            tangent.xyz += weight * unpackSnorm4x8(UIntRef(fetchAddress).data).xyz;
            break;
        case 10U: // SHORT normalized
            uvec2 fetched = UVec2Ref(fetchAddress).data;
            tangent.xyz += weight * vec3(unpackSnorm2x16(fetched.x), unpackSnorm2x16(fetched.y).x);
            break;
        }
    }

    return tangent;
}

#if TEXCOORD_COUNT >= 1 || HAS_BASE_COLOR_TEXTURE
vec2 getTexcoord(uint texcoordIndex, uint componentType, bool normalized){
    uvec2 fetchAddress = getFetchAddress(PRIMITIVE.texcoordAccessors[texcoordIndex], gl_VertexIndex);

    switch (componentType) {
    case 5120U: { // BYTE
        vec2 result = unpackSnorm4x8(UIntRef(fetchAddress).data).xy;
        if (!normalized) {
            // map [-1, 1] to [-128, 127]
            result = 127.5 * result - 0.5;
        }
        return result;
    }
    case 5121U: { // UNSIGNED BYTE
        vec2 result = unpackUnorm4x8(UIntRef(fetchAddress).data).xy;
        if (!normalized) {
            result *= 255.0;
        }
        return result;
    }
    case 5122U: { // SHORT
        vec2 result = unpackSnorm2x16(UIntRef(fetchAddress).data);
        if (!normalized) {
            // map [-1, 1] to [-32768, 32767]
            result = 32767.5 * result - 0.5;
        }
        return result;
    }
    case 5123U: { // UNSIGNED SHORT
        vec2 result = unpackUnorm2x16(UIntRef(fetchAddress).data);
        if (!normalized) {
            result *= 65535.0;
        }
        return result;
    }
    case 5126U: // FLOAT
        return Vec2Ref(fetchAddress).data;
    }
    return vec2(0.0); // unreachable.
}
#endif

#if HAS_COLOR_0_ATTRIBUTE
vec4 getColor0(uint componentType, uint componentCount) {
    uvec2 fetchAddress = add64(PRIMITIVE.color0Accessor.bufferAddress, PRIMITIVE.color0Accessor.stride * uint(gl_VertexIndex));
    if (componentCount == 3U) {
        switch (componentType) {
        case 5126U: // FLOAT
            return vec4(Vec3Ref(fetchAddress).data, 1.0);
        case 5121U: // UNSIGNED BYTE normalized
            return vec4(unpackUnorm4x8(UIntRef(fetchAddress).data).xyz, 1.0);
        case 5123U: // UNSIGNED SHORT normalized
            uvec2 fetched = UVec2Ref(fetchAddress).data;
            return vec4(unpackUnorm2x16(fetched.x), unpackUnorm2x16(fetched.y).x, 1.0);
        }
    }
    else if (componentCount == 4U) {
        switch (componentType) {
        case 5126U: // FLOAT
            return Vec4Ref(fetchAddress).data;
        case 5121U: // UNSIGNED BYTE normalized
            return unpackUnorm4x8(UIntRef(fetchAddress).data);
        case 5123U: // UNSIGNED SHORT normalized
            uvec2 fetched = UVec2Ref(fetchAddress).data;
            return vec4(unpackUnorm2x16(fetched.x), unpackUnorm2x16(fetched.y));
        }
    }
    return vec4(1.0); // unreachable.
}
#endif

#if HAS_COLOR_0_ALPHA_ATTRIBUTE
float getColor0Alpha(uint componentType) {
    // Here uint64_t address should not be used because adding the size of RGB components to it will make 64-bit
    // integer arithmetic instruction.
    uint fetchIndex = PRIMITIVE.color0Accessor.stride * uint(gl_VertexIndex);
    switch (componentType) {
    case 5126U: // FLOAT
        return FloatRef(add64(PRIMITIVE.color0Accessor.bufferAddress, fetchIndex + 12 /* skip rgb */)).data;
    case 5121U: // UNSIGNED BYTE normalized
        return unpackUnorm4x8(UIntRef(add64(PRIMITIVE.color0Accessor.bufferAddress, fetchIndex)).data).a;
    case 5123U: // UNSIGNED SHORT normalized
        return unpackUnorm2x16(UIntRef(add64(PRIMITIVE.color0Accessor.bufferAddress, fetchIndex + 4 /* skip rg */)).data).g;
    }
    return 1.0; // unreachable.
}
#endif

uvec4 getJoints(uint jointIndex){
    Accessor jointsAccessor = PRIMITIVE.jointAccessors.data[jointIndex];
    uvec2 fetchAddress = getFetchAddress(jointsAccessor, gl_VertexIndex);

    switch (jointsAccessor.componentType) {
    case 1U: { // UNSIGNED BYTE
        uint fetched = UIntRef(fetchAddress).data;
        return uvec4(bitfieldExtract(fetched, 0, 8), bitfieldExtract(fetched, 8, 8), bitfieldExtract(fetched, 16, 8), bitfieldExtract(fetched, 24, 8));
    }
    case 3U: { // UNSIGNED SHORT
        uvec2 fetched = UVec2Ref(fetchAddress).data;
        return uvec4(bitfieldExtract(fetched.x, 0, 16), bitfieldExtract(fetched.x, 16, 16), bitfieldExtract(fetched.y, 0, 16), bitfieldExtract(fetched.y, 16, 16));
    }
    }
    return uvec4(0); // unreachable.
}

vec4 getWeights(uint weightIndex){
    Accessor weightsAccessor = PRIMITIVE.weightAccessors.data[weightIndex];
    uvec2 fetchAddress = getFetchAddress(weightsAccessor, gl_VertexIndex);

    switch (weightsAccessor.componentType) {
    case 6U: // FLOAT
        return Vec4Ref(fetchAddress).data;
    case 9U: // UNSIGNED BYTE normalized
        return unpackUnorm4x8(UIntRef(fetchAddress).data);
    case 11U: // UNSIGNED SHORT normalized
        uvec2 fetched = UVec2Ref(fetchAddress).data;
        return vec4(unpackUnorm2x16(fetched.x), unpackUnorm2x16(fetched.y));
    }
    return vec4(0.0); // unreachable.
}

#endif // VERTEX_PULLING_GLSL