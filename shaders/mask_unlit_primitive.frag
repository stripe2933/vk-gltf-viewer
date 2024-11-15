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

// --------------------
// Functions.
// --------------------

float geometricMean(vec2 v){
    return sqrt(v.x * v.y);
}

void main(){
    vec4 baseColor = MATERIAL.baseColorFactor * texture(textures[int(MATERIAL.baseColorTextureIndex) + 1], inBaseColorTexcoord);

    float alpha = baseColor.a;
    alpha *= 1.0 + geometricMean(textureQueryLod(textures[int(MATERIAL.baseColorTextureIndex) + 1], inBaseColorTexcoord)) * 0.25;
    // Apply sharpness to the alpha.
    // See: https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f.
    alpha = (alpha - MATERIAL.alphaCutoff) / max(fwidth(alpha), 1e-4) + 0.5;

    outColor = vec4(baseColor.rgb, alpha);
}