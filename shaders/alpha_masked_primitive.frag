#version 460
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_scalar_block_layout : require

#include "spherical_harmonics.glsl"

// For convinience.
#define PRIMITIVE primitives[baseInstance]
#define MATERIAL materials[PRIMITIVE.materialIndex]

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);
const float ALPHA_CUTOFF = 0.5;

struct Material {
    uint8_t VERTEX_DATA[6];
    int16_t baseColorTextureIndex;
    int16_t metallicRoughnessTextureIndex;
    int16_t normalTextureIndex;
    int16_t occlusionTextureIndex;
    int16_t emissiveTextureIndex;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    vec3 emissiveFactor;
    bool doubleSided;
};

struct Primitive {
    uint8_t VERTEX_DATA[64];
    uint nodeIndex;
    uint materialIndex;
};

layout (location = 0) in vec3 fragPosition;
layout (location = 1) in mat3 fragTBN;
layout (location = 4) in vec2 fragBaseColorTexcoord;
layout (location = 5) in vec2 fragMetallicRoughnessTexcoord;
layout (location = 6) in vec2 fragNormalTexcoord;
layout (location = 7) in vec2 fragOcclusionTexcoord;
layout (location = 8) in vec2 fragEmissiveTexcoord;
layout (location = 9) flat in uint baseInstance;

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

layout (set = 2, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
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

void main(){
    vec4 baseColor = MATERIAL.baseColorFactor * texture(textures[int(MATERIAL.baseColorTextureIndex) + 1], fragBaseColorTexcoord);

    vec2 metallicRoughness = vec2(MATERIAL.metallicFactor, MATERIAL.roughnessFactor) * texture(textures[int(MATERIAL.metallicRoughnessTextureIndex) + 1], fragMetallicRoughnessTexcoord).bg;
    float metallic = metallicRoughness.x;
    float roughness = metallicRoughness.y;

    vec3 N;
    if (int(MATERIAL.normalTextureIndex) != -1){
        vec3 tangentNormal = texture(textures[int(MATERIAL.normalTextureIndex) + 1], fragNormalTexcoord).rgb;
        vec3 scaledNormal = (2.0 * tangentNormal - 1.0) * vec3(MATERIAL.normalScale, MATERIAL.normalScale, 1.0);
        N = normalize(fragTBN * scaledNormal);
    }
    else {
        N = normalize(fragTBN[2]);
    }

    float occlusion = 1.0 + MATERIAL.occlusionStrength * (texture(textures[int(MATERIAL.occlusionTextureIndex) + 1], fragOcclusionTexcoord).r - 1.0);

    vec3 emissive = MATERIAL.emissiveFactor * texture(textures[int(MATERIAL.emissiveTextureIndex) + 1], fragEmissiveTexcoord).rgb;

    vec3 V = normalize(pc.viewPosition - fragPosition);
    // If material is double-sided and normal is not facing the camera, normal have to be flipped.
    if (uint(MATERIAL.doubleSided) != 0U && dot(N, V) < 0.0) N = -N;
    vec3 R = reflect(-V, N);

    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = diffuseIrradiance(N);
    vec3 diffuse    = irradiance * baseColor.rgb;

    uint prefilteredmapMipLevels = textureQueryLevels(prefilteredmap);
    vec3 prefilteredColor = textureLod(prefilteredmap, R, roughness * (prefilteredmapMipLevels - 1U)).rgb;
    vec2 brdf  = texture(brdfmap, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 color = (kD * diffuse + specular) * occlusion;

    // Compare the luminance of color and emissive.
    // If emissive is brighter, use it.
    if (dot(emissive, REC_709_LUMA) > dot(color, REC_709_LUMA)){
        color = emissive;
    }

    float alpha = baseColor.a;
    // Apply sharpness to the alpha.
    // See: https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f.
    alpha = (alpha - ALPHA_CUTOFF) / max(fwidth(alpha), 1e-4) + 0.5;

    outColor = vec4(color, alpha);
}