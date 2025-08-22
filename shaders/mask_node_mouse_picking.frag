#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require
#if KHR_SHADER_ATOMIC_INT64 == 1
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#endif

#define FRAGMENT_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_IN HAS_BASE_COLOR_TEXTURE || HAS_COLOR_0_ALPHA_ATTRIBUTE

layout (constant_id = 0) const bool USE_TEXTURE_TRANSFORM = false;

layout (location = 0) flat in uint inNodeIndex;
layout (location = 1) flat in uint inMaterialIndex;
#if HAS_VARIADIC_IN
layout (location = 2) in FRAG_VARIADIC_IN {
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord;
#endif
#if HAS_COLOR_0_ALPHA_ATTRIBUTE
    float color0Alpha;
#endif
} variadic_in;
#endif

layout (set = 0, binding = 2, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
#if SEPARATE_IMAGE_SAMPLER == 1
layout (set = 0, binding = 3) uniform sampler samplers[];
layout (set = 0, binding = 4) uniform texture2D images[];
#else
layout (set = 0, binding = 3) uniform sampler2D textures[];
#endif

layout (set = 1, binding = 0) buffer MousePickingResultBuffer {
#if KHR_SHADER_ATOMIC_INT64 == 1
    uint64_t packedNodeIndexAndDepth;
#else
    uint packedNodeIndexAndDepth;
#endif
};

void main(){
    float baseColorAlpha = MATERIAL.baseColorFactor.a;
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord = variadic_in.baseColorTexcoord;
    if (USE_TEXTURE_TRANSFORM) {
        baseColorTexcoord = mat2(MATERIAL.baseColorTextureTransform) * baseColorTexcoord + MATERIAL.baseColorTextureTransform[2];
    }
#if SEPARATE_IMAGE_SAMPLER == 1
    baseColorAlpha *= texture(sampler2D(images[uint(MATERIAL.baseColorTextureIndex) & 0xFFFU], samplers[uint(MATERIAL.baseColorTextureIndex) >> 12U]), baseColorTexcoord).a;
#else
    baseColorAlpha *= texture(textures[uint(MATERIAL.baseColorTextureIndex)], baseColorTexcoord).a;
#endif
#endif
#if HAS_COLOR_0_ALPHA_ATTRIBUTE
    baseColorAlpha *= variadic_in.color0Alpha;
#endif
    if (baseColorAlpha < MATERIAL.alphaCutoff) discard;

#if KHR_SHADER_ATOMIC_INT64 == 1
    atomicMax(packedNodeIndexAndDepth, packUint2x32(uvec2(inNodeIndex, floatBitsToUint(gl_FragCoord.z))));
#else
    atomicMax(packedNodeIndexAndDepth, (uint(gl_FragCoord.z * 65535) << 16) | inNodeIndex);
#endif
}