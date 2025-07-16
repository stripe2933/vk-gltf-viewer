#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_scalar_block_layout : require
#if EXT_SHADER_STENCIL_EXPORT == 1
#extension GL_ARB_shader_stencil_export : require
#endif

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "spherical_harmonics.glsl"
#include "types.glsl"

#define HAS_VARIADIC_IN !FRAGMENT_SHADER_GENERATED_TBN || TEXCOORD_COUNT >= 1 || HAS_COLOR_ATTRIBUTE

layout (constant_id = 0) const uint PACKED_TEXTURE_TRANSFORMS = 0; // [FALSE, FALSE, FALSE, FALSE, FALSE]

layout (location = 0) in vec3 inPosition;
layout (location = 1) flat in uint inMaterialIndex;
#if HAS_VARIADIC_IN
layout (location = 2) in FS_VARIADIC_IN {
#if !FRAGMENT_SHADER_GENERATED_TBN
    mat3 tbn;
#endif

#if TEXCOORD_COUNT == 1
    vec2 texcoord;
#elif TEXCOORD_COUNT == 2
    mat2 texcoords;
#elif TEXCOORD_COUNT == 3
    mat3x2 texcoords;
#elif TEXCOORD_COUNT == 4
    mat4x2 texcoords;
#elif TEXCOORD_COUNT >= 5
#error "Maximum texcoord count exceeded."
#endif

#if HAS_COLOR_ATTRIBUTE
    vec4 color;
#endif
} variadic_in;
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

layout (set = 1, binding = 2, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
layout (set = 1, binding = 3) uniform sampler2D textures[];

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
    vec3 viewPosition;
} pc;

#if (ALPHA_MODE == 0 || ALPHA_MODE == 2) && (EXT_SHADER_STENCIL_EXPORT == 0)
layout (early_fragment_tests) in;
#endif

// --------------------
// Functions.
// --------------------

#if TEXCOORD_COUNT == 1
vec2 getTexcoord(uint texcoordIndex) {
    return variadic_in.texcoord;
}
#elif TEXCOORD_COUNT >= 2
vec2 getTexcoord(uint texcoordIndex) {
    return variadic_in.texcoords[texcoordIndex];
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

float trinaryMax(vec3 v) {
    return max(max(v.x, v.y), v.z);
}

vec3 tonemap(vec3 color) {
    return color / (1.0 + trinaryMax(color));
}

void writeOutput(vec4 color) {
#if ALPHA_MODE == 0
    outColor = vec4(color.rgb, 1.0);
#elif ALPHA_MODE == 1
#if TEXCOORD_COUNT >= 1
    color.a *= 1.0 + geometricMean(textureQueryLod(textures[uint(MATERIAL.baseColorTextureIndex)], getTexcoord(MATERIAL.baseColorTexcoordIndex))) * 0.25;
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
    vec2 baseColorTexcoord = getTexcoord(MATERIAL.baseColorTexcoordIndex);
    if ((PACKED_TEXTURE_TRANSFORMS & 1U) == 1U) {
        baseColorTexcoord = mat2(MATERIAL.baseColorTextureTransform) * baseColorTexcoord + MATERIAL.baseColorTextureTransform[2];
    }
    baseColor *= texture(textures[uint(MATERIAL.baseColorTextureIndex)], baseColorTexcoord);
#endif
#if HAS_COLOR_ATTRIBUTE
    baseColor *= variadic_in.color;
#endif

    float metallic = MATERIAL.metallicFactor;
    float roughness = MATERIAL.roughnessFactor;
#if TEXCOORD_COUNT >= 1
    vec2 metallicRoughnessTexcoord = getTexcoord(MATERIAL.metallicRoughnessTexcoordIndex);
    if ((PACKED_TEXTURE_TRANSFORMS & 2U) == 2U) {
        metallicRoughnessTexcoord = mat2(MATERIAL.metallicRoughnessTextureTransform) * metallicRoughnessTexcoord + MATERIAL.metallicRoughnessTextureTransform[2];
    }
    vec2 metallicRoughness = texture(textures[uint(MATERIAL.metallicRoughnessTextureIndex)], metallicRoughnessTexcoord).bg;
    metallic *= metallicRoughness.x;
    roughness *= metallicRoughness.y;
#endif

    vec3 N;
#if FRAGMENT_SHADER_GENERATED_TBN
    vec3 tangent = dFdx(inPosition);
    vec3 bitangent = dFdy(inPosition);
    N = normalize(cross(tangent, bitangent));

#if TEXCOORD_COUNT >= 1
    if (MATERIAL.normalTextureIndex != 0US){
        vec2 normalTexcoord = getTexcoord(MATERIAL.normalTexcoordIndex);
        if ((PACKED_TEXTURE_TRANSFORMS & 4U) == 4U) {
            normalTexcoord = mat2(MATERIAL.normalTextureTransform) * normalTexcoord + MATERIAL.normalTextureTransform[2];
        }
        vec3 tangentNormal = texture(textures[uint(MATERIAL.normalTextureIndex)], normalTexcoord).rgb;
        vec3 scaledNormal = (2.0 * tangentNormal - 1.0) * vec3(MATERIAL.normalScale, MATERIAL.normalScale, 1.0);
        N = normalize(mat3(tangent, bitangent, N) * scaledNormal);
    }
#endif
#elif TEXCOORD_COUNT >= 1
    if (MATERIAL.normalTextureIndex != 0US){
        vec2 normalTexcoord = getTexcoord(MATERIAL.normalTexcoordIndex);
        if ((PACKED_TEXTURE_TRANSFORMS & 4U) == 4U) {
            normalTexcoord = mat2(MATERIAL.normalTextureTransform) * normalTexcoord + MATERIAL.normalTextureTransform[2];
        }
        vec3 tangentNormal = texture(textures[uint(MATERIAL.normalTextureIndex)], normalTexcoord).rgb;
        vec3 scaledNormal = (2.0 * tangentNormal - 1.0) * vec3(MATERIAL.normalScale, MATERIAL.normalScale, 1.0);
        N = normalize(variadic_in.tbn * scaledNormal);
    }
    else {
        N = normalize(variadic_in.tbn[2]);
    }
#else
    N = normalize(variadic_in.tbn[2]);
#endif

    float occlusion = MATERIAL.occlusionStrength;
#if TEXCOORD_COUNT >= 1
    vec2 occlusionTexcoord = getTexcoord(MATERIAL.occlusionTexcoordIndex);
    if ((PACKED_TEXTURE_TRANSFORMS & 8U) == 8U) {
        occlusionTexcoord = mat2(MATERIAL.occlusionTextureTransform) * occlusionTexcoord + MATERIAL.occlusionTextureTransform[2];
    }
    occlusion = 1.0 + MATERIAL.occlusionStrength * (texture(textures[uint(MATERIAL.occlusionTextureIndex)], occlusionTexcoord).r - 1.0);
#endif

    vec3 emissive = MATERIAL.emissive;
#if TEXCOORD_COUNT >= 1
    vec2 emissiveTexcoord = getTexcoord(MATERIAL.emissiveTexcoordIndex);
    if ((PACKED_TEXTURE_TRANSFORMS & 16U) == 16U) {
        emissiveTexcoord = mat2(MATERIAL.emissiveTextureTransform) * emissiveTexcoord + MATERIAL.emissiveTextureTransform[2];
    }
    emissive *= texture(textures[uint(MATERIAL.emissiveTextureIndex)], emissiveTexcoord).rgb;
#endif

#if EXT_SHADER_STENCIL_EXPORT == 1
    gl_FragStencilRefARB = trinaryMax(emissive) > 1.0 ? 1 : 0;
#endif

    vec3 V = normalize(pc.viewPosition - inPosition);
    float NdotV = dot(N, V);
    // If normal is not facing the camera, normal have to be flipped.
    if (NdotV < 0.0) {
        N = -N;
        NdotV = -NdotV;
    }
    vec3 R = reflect(-V, N);

    float dielectric_f0 = (MATERIAL.ior - 1.0) / (MATERIAL.ior + 1.0);
    dielectric_f0 = dielectric_f0 * dielectric_f0;

    vec3 F0 = mix(vec3(dielectric_f0), baseColor.rgb, metallic);
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

    writeOutput(vec4(tonemap(color), baseColor.a));
}