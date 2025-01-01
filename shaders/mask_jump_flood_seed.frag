#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "types.glsl"

layout (location = 0) flat in uint inMaterialIndex;
#if HAS_BASE_COLOR_TEXTURE
layout (location = 1) in vec2 inBaseColorTexcoord;
#endif

layout (location = 0) out uvec2 outCoordinate;

layout (set = 0, binding = 1, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
layout (set = 0, binding = 2) uniform sampler2D textures[];

void main(){
    float baseColorAlpha = MATERIAL.baseColorFactor.a;
#if HAS_BASE_COLOR_TEXTURE
    baseColorAlpha *= texture(textures[uint(MATERIAL.baseColorTextureIndex) + 1], inBaseColorTexcoord).a;
#endif
    if (baseColorAlpha < MATERIAL.alphaCutoff) discard;

    outCoordinate = uvec2(gl_FragCoord.xy);
}