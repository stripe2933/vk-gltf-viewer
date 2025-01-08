#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_IN HAS_BASE_COLOR_TEXTURE || HAS_COLOR_ALPHA_ATTRIBUTE

layout (constant_id = 0) const uint TEXTURE_TRANSFORM_TYPE = 0; // NONE

layout (location = 0) flat in uint inNodeIndex;
layout (location = 1) flat in uint inMaterialIndex;
#if HAS_VARIADIC_IN
layout (location = 2) in FRAG_VARIADIC_IN {
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord;
#endif
#if HAS_COLOR_ALPHA_ATTRIBUTE
    float colorAlpha;
#endif
} variadic_in;
#endif

layout (location = 0) out uint outNodeIndex;

layout (set = 0, binding = 1, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
layout (set = 0, binding = 2) uniform sampler2D textures[];

void main(){
    float baseColorAlpha = MATERIAL.baseColorFactor.a;
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord = variadic_in.baseColorTexcoord;
    if (TEXTURE_TRANSFORM_TYPE == 1) {
        baseColorTexcoord = vec2(MATERIAL.baseColorTextureTransformUpperLeft2x2[0][0], MATERIAL.baseColorTextureTransformUpperLeft2x2[0][1]) * baseColorTexcoord + MATERIAL.baseColorTextureTransformOffset;
    }
    else if (TEXTURE_TRANSFORM_TYPE == 2) {
        baseColorTexcoord = MATERIAL.baseColorTextureTransformUpperLeft2x2 * baseColorTexcoord + MATERIAL.baseColorTextureTransformOffset;
    }
    baseColorAlpha *= texture(textures[uint(MATERIAL.baseColorTextureIndex) + 1], baseColorTexcoord).a;
#endif
#if HAS_COLOR_ALPHA_ATTRIBUTE
    baseColorAlpha *= variadic_in.colorAlpha;
#endif
    if (baseColorAlpha < MATERIAL.alphaCutoff) discard;

    outNodeIndex = inNodeIndex;
}