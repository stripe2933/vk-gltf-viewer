#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_scalar_block_layout : require

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "spherical_harmonics.glsl"
#include "types.glsl"

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);

layout (location = 0) in vec3 inPosition;
layout (location = 1) flat in uint inMaterialIndex;
#if TEXCOORD_COUNT >= 1
layout (location = 2) in vec2 inTexcoord0;
#endif
#if TEXCOORD_COUNT >= 2
layout (location = 3) in vec2 inTexcoord1;
#endif
#if TEXCOORD_COUNT >= 3
layout (location = 4) in vec2 inTexcoord2;
#endif
#if TEXCOORD_COUNT >= 4
layout (location = 5) in vec2 inTexcoord3;
#endif
#if TEXCOORD_COUNT >= 5
layout (location = 6) in vec2 inTexcoord4;
#endif
#if TEXCOORD_COUNT >= 6
#error "Maximum texcoord count exceeded."
#endif
#if !FRAGMENT_SHADER_GENERATED_TBN
layout (location = TEXCOORD_COUNT + 2) in mat3 inTBN;
#endif

layout (location = 0) out vec4 outColor;
#if ALPHA_MODE == 2
layout (location = 1) out float outRevealage;
#endif

layout (set = 0, binding = 0, scalar) uniform SphericalHarmonicsBuffer {
    vec3 coefficients[9];
} sphericalHarmonics;
layout (set = 0, binding = 1) uniform samplerCube prefilteredmap;
layout (set = 0, binding = 2) uniform sampler2D brdfmap;

layout (set = 1, binding = 1, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
layout (set = 1, binding = 2) uniform sampler2D textures[];

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
    vec3 viewPosition;
} pc;

#if ALPHA_MODE == 0 || ALPHA_MODE == 2
layout (early_fragment_tests) in;
#endif

// --------------------
// Functions.
// --------------------

#if TEXCOORD_COUNT >= 1
vec2 getTexcoord(uint texcoordIndex) {
    switch (texcoordIndex) {
        case 0: return inTexcoord0;
#if TEXCOORD_COUNT >= 2
        case 1: return inTexcoord1;
#endif
#if TEXCOORD_COUNT >= 3
        case 2: return inTexcoord2;
#endif
#if TEXCOORD_COUNT >= 4
        case 3: return inTexcoord3;
#endif
#if TEXCOORD_COUNT >= 5
        case 4: return inTexcoord4;
#endif
    }
    return vec2(0.0);
}
#endif

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness){
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 diffuseIrradiance(vec3 normal){
    SphericalHarmonicBasis basis = SphericalHarmonicBasis_construct(normal);
    return SphericalHarmonicBasis_restore(basis, sphericalHarmonics.coefficients) / 3.141593;
}

float geometricMean(vec2 v){
    return sqrt(v.x * v.y);
}

void writeOutput(vec4 color) {
#if ALPHA_MODE == 0
    outColor = vec4(color.rgb, 1.0);
#elif ALPHA_MODE == 1
#if TEXCOORD_COUNT >= 1
    color.a *= 1.0 + geometricMean(textureQueryLod(textures[int(MATERIAL.baseColorTextureIndex) + 1], getTexcoord(MATERIAL.baseColorTexcoordIndex))) * 0.25;
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
#if TEXCOORD_COUNT >= 1
    baseColor *= texture(textures[int(MATERIAL.baseColorTextureIndex) + 1], getTexcoord(MATERIAL.baseColorTexcoordIndex));
#endif

    float metallic = MATERIAL.metallicFactor;
    float roughness = MATERIAL.roughnessFactor;
#if TEXCOORD_COUNT >= 1
    vec2 metallicRoughness = texture(textures[int(MATERIAL.metallicRoughnessTextureIndex) + 1], getTexcoord(MATERIAL.metallicRoughnessTexcoordIndex)).bg;
    metallic *= metallicRoughness.x;
    roughness *= metallicRoughness.y;
#endif

    vec3 N;
#if FRAGMENT_SHADER_GENERATED_TBN
    vec3 tangent = dFdx(inPosition);
    vec3 bitangent = dFdy(inPosition);
    N = normalize(cross(tangent, bitangent));

#if TEXCOORD_COUNT >= 1
    if (int(MATERIAL.normalTextureIndex) != -1){
        vec3 tangentNormal = texture(textures[int(MATERIAL.normalTextureIndex) + 1], getTexcoord(MATERIAL.normalTexcoordIndex)).rgb;
        vec3 scaledNormal = (2.0 * tangentNormal - 1.0) * vec3(MATERIAL.normalScale, MATERIAL.normalScale, 1.0);
        N = normalize(mat3(tangent, bitangent, N) * scaledNormal);
    }
#endif
#elif TEXCOORD_COUNT >= 1
    if (int(MATERIAL.normalTextureIndex) != -1){
        vec3 tangentNormal = texture(textures[int(MATERIAL.normalTextureIndex) + 1], getTexcoord(MATERIAL.normalTexcoordIndex)).rgb;
        vec3 scaledNormal = (2.0 * tangentNormal - 1.0) * vec3(MATERIAL.normalScale, MATERIAL.normalScale, 1.0);
        N = normalize(inTBN * scaledNormal);
    }
    else {
        N = normalize(inTBN[2]);
    }
#else
    N = normalize(inTBN[2]);
#endif

    float occlusion = MATERIAL.occlusionStrength;
#if TEXCOORD_COUNT >= 1
    occlusion = 1.0 + MATERIAL.occlusionStrength * (texture(textures[int(MATERIAL.occlusionTextureIndex) + 1], getTexcoord(MATERIAL.occlusionTexcoordIndex)).r - 1.0);
#endif

    vec3 emissive = MATERIAL.emissiveFactor;
#if TEXCOORD_COUNT >= 1
    emissive *= texture(textures[int(MATERIAL.emissiveTextureIndex) + 1], getTexcoord(MATERIAL.emissiveTexcoordIndex)).rgb;
#endif

    vec3 V = normalize(pc.viewPosition - inPosition);
    float NdotV = dot(N, V);
    // If normal is not facing the camera, normal have to be flipped.
    if (NdotV < 0.0) {
        N = -N;
        NdotV = -NdotV;
    }
    vec3 R = reflect(-V, N);

    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
    float maxNdotV = max(NdotV, 0.0);
    vec3 F = fresnelSchlickRoughness(maxNdotV, F0, roughness);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = diffuseIrradiance(N);
    vec3 diffuse    = irradiance * baseColor.rgb;

    uint prefilteredmapMipLevels = textureQueryLevels(prefilteredmap);
    vec3 prefilteredColor = textureLod(prefilteredmap, R, roughness * (prefilteredmapMipLevels - 1U)).rgb;
    vec2 brdf  = texture(brdfmap, vec2(maxNdotV, roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 color = (kD * diffuse + specular) * occlusion + emissive;

    // Tone mapping using REC.709 luma.
    float colorLuminance = dot(color, REC_709_LUMA);
    vec3 correctedColor = color / (1.0 + colorLuminance);

    writeOutput(vec4(correctedColor, baseColor.a));
}