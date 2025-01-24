#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

#include "branch.glsl"
#define VERTEX_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_OUT !FRAGMENT_SHADER_GENERATED_TBN || TEXCOORD_COUNT >= 1 || HAS_COLOR_ATTRIBUTE

layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec2Ref { u8vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec3Ref { u8vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec4Ref { u8vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec2Ref { u16vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec3Ref { u16vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec4Ref { u16vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec3Ref { vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec4Ref { vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 64) readonly buffer Node { mat4 transforms[]; };

layout (location = 0) out vec3 outPosition;
layout (location = 1) flat out uint outMaterialIndex;
#if HAS_VARIADIC_OUT
layout (location = 2) out VS_VARIADIC_OUT {
#if !FRAGMENT_SHADER_GENERATED_TBN
    mat3 tbn;
#endif

#if TEXCOORD_COUNT == 1
    vec2 texcoord;
#elif TEXCOORD_COUNT == 2
    mat2 texcoords;
#elif TEXCOORD_COUNT == 3
    mat3x2 texcoords;
#elif TEXCOORD_COUNT == 4
    mat4x2 texcoords;
#elif TEXCOORD_COUNT >= 5
#error "Maximum texcoord count exceeded."
#endif

#if HAS_COLOR_ATTRIBUTE
    vec4 color;
#endif
} variadic_out;
#endif

layout (set = 1, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 1, binding = 1, std430) readonly buffer MaterialBuffer {
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

vec3 getPosition() {
    return Vec3Ref(PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * uint(gl_VertexIndex)).data;
}

#if !FRAGMENT_SHADER_GENERATED_TBN
vec3 getNormal() {
    return Vec3Ref(PRIMITIVE.pNormalBuffer + uint(PRIMITIVE.normalByteStride) * uint(gl_VertexIndex)).data;
}

vec4 getTangent() {
    return Vec4Ref(PRIMITIVE.pTangentBuffer + uint(PRIMITIVE.tangentByteStride) * uint(gl_VertexIndex)).data;
}
#endif

#if TEXCOORD_COUNT >= 1
vec2 getTexcoord(uint texcoordIndex){
    IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.texcoordAttributeMappingInfos.data[texcoordIndex];
    uint64_t fetchAddress = mappingInfo.bytesPtr + mappingInfo.stride * uint(gl_VertexIndex);

    if (mappingInfo.componentType == uint8_t(6)) { // 5126: FLOAT
        return Vec2Ref(fetchAddress).data;
    }
    if (mappingInfo.componentType == uint8_t(3)) { // 5123: UNSIGNED SHORT
        return vec2(U16Vec2Ref(fetchAddress).data) / 65535.0;
    }
    if last_branch(mappingInfo.componentType == uint8_t(1)) { // 5121: UNSIGNED BYTE
        return vec2(U8Vec2Ref(fetchAddress).data) / 255.0;
    }
}
#endif

#if HAS_COLOR_ATTRIBUTE
vec4 getColor() {
    uint64_t fetchAddress = PRIMITIVE.pColorBuffer + uint(PRIMITIVE.colorByteStride) * uint(gl_VertexIndex);
    if (PRIMITIVE.colorComponentCount == uint8_t(4)) {
        if (PRIMITIVE.colorComponentType == uint8_t(6)) { // 5126: FLOAT
            return Vec4Ref(fetchAddress).data;
        }
        if (PRIMITIVE.colorComponentType == uint8_t(3)) { // 5123: UNSIGNED SHORT
            return vec4(U16Vec4Ref(fetchAddress).data) / 65535.0;
        }
        if last_branch(PRIMITIVE.colorComponentType == uint8_t(1)) { // 5121: UNSIGNED BYTE
            return vec4(U8Vec4Ref(fetchAddress).data) / 255.0;
        }
    }
    if last_branch(PRIMITIVE.colorComponentCount == uint8_t(3)) {
        if (PRIMITIVE.colorComponentType == uint8_t(6)) { // 5126: FLOAT
            return vec4(Vec3Ref(fetchAddress).data, 1.0);
        }
        if (PRIMITIVE.colorComponentType == uint8_t(3)) { // 5123: UNSIGNED SHORT
            return vec4(vec3(U16Vec4Ref(fetchAddress).data) / 65535.0, 1.0);
        }
        if last_branch(PRIMITIVE.colorComponentType == uint8_t(1)) { // 5121: UNSIGNED BYTE
            return vec4(vec3(U8Vec4Ref(fetchAddress).data) / 255.0, 1.0);
        }
    }
}
#endif

void main(){
    vec3 inPosition = getPosition();
    outPosition = (TRANSFORM * vec4(inPosition, 1.0)).xyz;

    outMaterialIndex = MATERIAL_INDEX;

#if !FRAGMENT_SHADER_GENERATED_TBN
    vec3 inNormal = getNormal();
    variadic_out.tbn[2] = normalize(mat3(TRANSFORM) * inNormal); // N

    if (int(MATERIAL.normalTextureIndex) != -1){
        vec4 inTangent = getTangent();
        variadic_out.tbn[0] = normalize(mat3(TRANSFORM) * inTangent.xyz); // T
        variadic_out.tbn[1] = cross(variadic_out.tbn[2], variadic_out.tbn[0]) * -inTangent.w; // B
    }
#endif

#if TEXCOORD_COUNT == 1
    variadic_out.texcoord = getTexcoord(0);
#elif TEXCOORD_COUNT >= 2
    for (uint i = 0; i < TEXCOORD_COUNT; i++){
        variadic_out.texcoords[i] = getTexcoord(i);
    }
#endif

#if HAS_COLOR_ATTRIBUTE
    variadic_out.color = getColor();
#endif

    gl_Position = pc.projectionView * vec4(outPosition, 1.0);
}