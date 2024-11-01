#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "types.glsl"

layout (location = 0) in vec2 inBaseColorTexcoord;
layout (location = 1) flat in uint inMaterialIndex;

layout (location = 0) out uvec2 outCoordinate;

layout (set = 0, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};
layout (set = 0, binding = 2) uniform sampler2D textures[];

void main(){
    float baseColorAlpha = MATERIAL.baseColorFactor.a * texture(textures[uint(MATERIAL.baseColorTextureIndex) + 1], inBaseColorTexcoord).a;
    if (baseColorAlpha < MATERIAL.alphaCutoff) discard;

    outCoordinate = uvec2(gl_FragCoord.xy);
}