#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_8bit_storage : require

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "types.glsl"

layout (location = 0) flat in uint inMaterialIndex;
#if HAS_BASE_COLOR_TEXTURE
layout (location = 1) in vec2 inBaseColorTexcoord;
#endif

layout (location = 0) out vec4 outColor;
#if ALPHA_MODE == 2
layout (location = 1) out float outRevealage;
#endif

layout (set = 1, binding = 1, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
layout (set = 1, binding = 2) uniform sampler2D textures[];

#if ALPHA_MODE == 0 || ALPHA_MODE == 2
layout (early_fragment_tests) in;
#endif

float geometricMean(vec2 v){
    return sqrt(v.x * v.y);
}

void writeOutput(vec4 color) {
#if ALPHA_MODE == 0
    outColor = vec4(color.rgb, 1.0);
#elif ALPHA_MODE == 1
#if HAS_BASE_COLOR_TEXTURE
    color.a *= 1.0 + geometricMean(textureQueryLod(textures[int(MATERIAL.baseColorTextureIndex) + 1], inBaseColorTexcoord)) * 0.25;
    // Apply sharpness to the alpha.
    // See: https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f.
    color.a = (color.a - MATERIAL.alphaCutoff) / max(fwidth(color.a), 1e-4) + 0.5;
#else
    color.a = color.a >= MATERIAL.alphaCutoff ? 1 : 0;
#endif
    outColor = color;
#elif ALPHA_MODE == 2
    // Weighted Blended.
    float weight = clamp(
        pow(min(1.0, color.a * 10.0) + 0.01, 3.0) * 1e8 * pow(1.0 - gl_FragCoord.z * 0.9, 3.0),
        1e-2, 3e3);
    outColor = vec4(color.rgb * color.a, color.a) * weight;
    outRevealage = color.a;
#endif
}

void main(){
    vec4 baseColor = MATERIAL.baseColorFactor;
#if HAS_BASE_COLOR_TEXTURE
    baseColor *= texture(textures[int(MATERIAL.baseColorTextureIndex) + 1], inBaseColorTexcoord);
#endif

    writeOutput(baseColor);
}