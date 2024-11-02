#version 460
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_scalar_block_layout : require

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "spherical_harmonics.glsl"
#include "types.glsl"

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);

layout (location = 0) in vec3 inPosition;
layout (location = 1) in mat3 inTBN;
layout (location = 4) in vec2 inBaseColorTexcoord;
layout (location = 5) in vec2 inMetallicRoughnessTexcoord;
layout (location = 6) in vec2 inNormalTexcoord;
layout (location = 7) in vec2 inOcclusionTexcoord;
layout (location = 8) in vec2 inEmissiveTexcoord;
layout (location = 9) flat in uint inMaterialIndex;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0, scalar) uniform SphericalHarmonicsBuffer {
    vec3 coefficients[9];
} sphericalHarmonics;
layout (set = 0, binding = 1) uniform samplerCube prefilteredmap;
layout (set = 0, binding = 2) uniform sampler2D brdfmap;

layout (set = 1, binding = 0) uniform sampler2D textures[];
layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
    vec3 viewPosition;
} pc;

// --------------------
// Functions.
// --------------------

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

void main(){
    vec4 baseColor = MATERIAL.baseColorFactor * texture(textures[int(MATERIAL.baseColorTextureIndex) + 1], inBaseColorTexcoord);

    vec2 metallicRoughness = vec2(MATERIAL.metallicFactor, MATERIAL.roughnessFactor) * texture(textures[int(MATERIAL.metallicRoughnessTextureIndex) + 1], inMetallicRoughnessTexcoord).bg;
    float metallic = metallicRoughness.x;
    float roughness = metallicRoughness.y;

    vec3 N;
    if (int(MATERIAL.normalTextureIndex) != -1){
        vec3 tangentNormal = texture(textures[int(MATERIAL.normalTextureIndex) + 1], inNormalTexcoord).rgb;
        vec3 scaledNormal = (2.0 * tangentNormal - 1.0) * vec3(MATERIAL.normalScale, MATERIAL.normalScale, 1.0);
        N = normalize(inTBN * scaledNormal);
    }
    else {
        N = normalize(inTBN[2]);
    }

    float occlusion = 1.0 + MATERIAL.occlusionStrength * (texture(textures[int(MATERIAL.occlusionTextureIndex) + 1], inOcclusionTexcoord).r - 1.0);

    vec3 emissive = MATERIAL.emissiveFactor * texture(textures[int(MATERIAL.emissiveTextureIndex) + 1], inEmissiveTexcoord).rgb;

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

    float alpha = baseColor.a;
    alpha *= 1.0 + geometricMean(textureQueryLod(textures[int(MATERIAL.baseColorTextureIndex) + 1], inBaseColorTexcoord)) * 0.25;
    // Apply sharpness to the alpha.
    // See: https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f.
    alpha = (alpha - MATERIAL.alphaCutoff) / max(fwidth(alpha), 1e-4) + 0.5;

    outColor = vec4(correctedColor, alpha);
}