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

layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec3Ref { vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec4Ref { vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 64) readonly buffer Node { mat4 transforms[]; };

layout (location = 0) out vec3 outPosition;
layout (location = 1) flat out uint outMaterialIndex;
#if TEXCOORD_COUNT == 1
layout (location = 2) out vec2 outTexcoord;
#elif TEXCOORD_COUNT == 2
layout (location = 2) out mat2 outTexcoords;
#elif TEXCOORD_COUNT == 3
layout (location = 2) out mat3x2 outTexcoords;
#elif TEXCOORD_COUNT == 4
layout (location = 2) out mat4x2 outTexcoords;
#elif TEXCOORD_COUNT >= 5
#error "Maximum texcoord count exceeded."
#endif
#if !FRAGMENT_SHADER_GENERATED_TBN
layout (location = TEXCOORD_COUNT + 2) out mat3 outTBN;
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

void main(){
    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + int(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    outPosition = (TRANSFORM * vec4(inPosition, 1.0)).xyz;

    outMaterialIndex = MATERIAL_INDEX;

#if TEXCOORD_COUNT == 1
    outTexcoord = getTexcoord(0);
#elif TEXCOORD_COUNT >= 2
    for (uint i = 0; i < TEXCOORD_COUNT; i++){
        outTexcoords[i] = getTexcoord(i);
    }
#endif

#if !FRAGMENT_SHADER_GENERATED_TBN
    vec3 inNormal = getVec3(PRIMITIVE.pNormalBuffer + int(PRIMITIVE.normalByteStride) * gl_VertexIndex);
    outTBN[2] = normalize(mat3(TRANSFORM) * inNormal); // N

    if (int(MATERIAL.normalTextureIndex) != -1){
        vec4 inTangent = getVec4(PRIMITIVE.pTangentBuffer + int(PRIMITIVE.tangentByteStride) * gl_VertexIndex);
        outTBN[0] = normalize(mat3(TRANSFORM) * inTangent.xyz); // T
        outTBN[1] = cross(outTBN[2], outTBN[0]) * -inTangent.w; // B
    }
#endif

    gl_Position = pc.projectionView * vec4(outPosition, 1.0);
}