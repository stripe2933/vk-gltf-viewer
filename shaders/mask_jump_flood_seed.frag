#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_IN HAS_BASE_COLOR_TEXTURE || HAS_COLOR_ALPHA_ATTRIBUTE

layout (constant_id = 0) const uint TEXTURE_TRANSFORM_TYPE = 0; // NONE

layout (location = 0) flat in uint inMaterialIndex;
#if HAS_VARIADIC_IN
layout (location = 1) in FRAG_VARIDIC_IN {
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord;
#endif
#if HAS_COLOR_ALPHA_ATTRIBUTE
    float colorAlpha;
#endif
} variadic_in;
#endif

layout (location = 0) out uvec2 outCoordinate;

layout (set = 0, binding = 4, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
layout (set = 0, binding = 5) uniform sampler2D textures[];

void main(){
    float baseColorAlpha = MATERIAL.baseColorFactor.a;
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord = variadic_in.baseColorTexcoord;
    if (TEXTURE_TRANSFORM_TYPE == 1) {
        baseColorTexcoord = vec2(MATERIAL.baseColorTextureTransform[0][0], MATERIAL.baseColorTextureTransform[1][1]) * baseColorTexcoord + MATERIAL.baseColorTextureTransform[2];
    }
    else if (TEXTURE_TRANSFORM_TYPE == 2) {
        baseColorTexcoord = mat2(MATERIAL.baseColorTextureTransform) * baseColorTexcoord + MATERIAL.baseColorTextureTransform[2];
    }
    baseColorAlpha *= texture(textures[uint(MATERIAL.baseColorTextureIndex)], baseColorTexcoord).a;
#endif
#if HAS_COLOR_ALPHA_ATTRIBUTE
    baseColorAlpha *= variadic_in.colorAlpha;
#endif
    if (baseColorAlpha < MATERIAL.alphaCutoff) discard;

    outCoordinate = uvec2(gl_FragCoord.xy);
}