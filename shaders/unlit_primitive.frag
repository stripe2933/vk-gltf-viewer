#version 460
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_8bit_storage : require

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "types.glsl"

layout (location = 0) in vec2 inBaseColorTexcoord;
layout (location = 1) flat in uint inMaterialIndex;

layout (location = 0) out vec4 outColor;

layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};
layout (set = 1, binding = 2) uniform sampler2D textures[];

layout (early_fragment_tests) in;

void main(){
    vec4 baseColor = MATERIAL.baseColorFactor * texture(textures[int(MATERIAL.baseColorTextureIndex) + 1], inBaseColorTexcoord);
    outColor = vec4(baseColor.rgb, 1.0);
}