#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_IN HAS_BASE_COLOR_TEXTURE || HAS_COLOR_ALPHA_ATTRIBUTE

layout (constant_id = 0) const bool BASE_COLOR_TEXTURE_TRANSFORM = false;

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

layout (set = 0, binding = 6, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
layout (set = 0, binding = 7) uniform sampler2D textures[];

void main(){
    float baseColorAlpha = MATERIAL.baseColorFactor.a;
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord = variadic_in.baseColorTexcoord;
    if (BASE_COLOR_TEXTURE_TRANSFORM) {
        baseColorTexcoord = mat2(MATERIAL.baseColorTextureTransform) * baseColorTexcoord + MATERIAL.baseColorTextureTransform[2];
    }
    baseColorAlpha *= texture(textures[uint(MATERIAL.baseColorTextureIndex)], baseColorTexcoord).a;
#endif
#if HAS_COLOR_ALPHA_ATTRIBUTE
    baseColorAlpha *= variadic_in.colorAlpha;
#endif
    if (baseColorAlpha < MATERIAL.alphaCutoff) discard;

    outNodeIndex = inNodeIndex;
}