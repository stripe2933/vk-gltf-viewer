#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_KHR_shader_subgroup_vote : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require

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

layout (set = 1, binding = 2, std430) readonly buffer MaterialBuffer {
    Material materials[];
};
#if SEPARATE_IMAGE_SAMPLER == 1
layout (set = 1, binding = 3) uniform sampler samplers[];
layout (set = 1, binding = 4) uniform texture2D images[];
#else
layout (set = 1, binding = 3) uniform sampler2D textures[];
#endif

layout (set = 2, binding = 0) buffer MousePickingResultBuffer {
    uint packedBits[];
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

    uint blockIndex = inNodeIndex >> 5U;
    uint bitmask = 1U << (inNodeIndex & 31U);

    if (subgroupAllEqual(blockIndex)) {
        bitmask = subgroupOr(bitmask);
        if (subgroupElect()) {
            atomicOr(packedBits[blockIndex], bitmask);
        }
    }
    else {
        atomicOr(packedBits[blockIndex], bitmask);
    }
}