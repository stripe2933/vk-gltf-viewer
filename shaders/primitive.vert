#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

#define VERTEX_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_OUT !FRAGMENT_SHADER_GENERATED_TBN || TEXCOORD_COUNT >= 1 || HAS_COLOR_ATTRIBUTE

layout (constant_id = 0) const uint COLOR_COMPONENT_COUNT = 0;

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

vec3 getVec3(uint64_t address){
    return Vec3Ref(address).data;
}

#if !FRAGMENT_SHADER_GENERATED_TBN
vec4 getVec4(uint64_t address){
    return Vec4Ref(address).data;
}
#endif

#if TEXCOORD_COUNT >= 1
vec2 getVec2(uint64_t address){
    return Vec2Ref(address).data;
}

vec2 getTexcoord(uint texcoordIndex){
    IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.texcoordAttributeMappingInfos.data[texcoordIndex];
    return getVec2(mappingInfo.bytesPtr + int(mappingInfo.stride) * gl_VertexIndex);
}
#endif

#if HAS_COLOR_ATTRIBUTE
vec4 getColor() {
    if (COLOR_COMPONENT_COUNT == 4) {
        return Vec4Ref(PRIMITIVE.pColorBuffer + int(PRIMITIVE.colorByteStride) * gl_VertexIndex).data;
    }
    else {
        return vec4(Vec3Ref(PRIMITIVE.pColorBuffer + int(PRIMITIVE.colorByteStride) * gl_VertexIndex).data, 1.0);
    }
}
#endif

void main(){
    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + int(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    outPosition = (TRANSFORM * vec4(inPosition, 1.0)).xyz;

    outMaterialIndex = MATERIAL_INDEX;

#if !FRAGMENT_SHADER_GENERATED_TBN
    vec3 inNormal = getVec3(PRIMITIVE.pNormalBuffer + int(PRIMITIVE.normalByteStride) * gl_VertexIndex);
    variadic_out.tbn[2] = normalize(mat3(TRANSFORM) * inNormal); // N

    if (int(MATERIAL.normalTextureIndex) != -1){
        vec4 inTangent = getVec4(PRIMITIVE.pTangentBuffer + int(PRIMITIVE.tangentByteStride) * gl_VertexIndex);
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