#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

#define VERTEX_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_OUT HAS_BASE_COLOR_TEXTURE || HAS_COLOR_ALPHA_ATTRIBUTE

layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer FloatRef { float data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec3Ref { vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 64) readonly buffer Node { mat4 transforms[]; };

layout (location = 0) flat out uint outNodeIndex;
layout (location = 1) flat out uint outMaterialIndex;
#if HAS_VARIADIC_OUT
layout (location = 2) out VS_VARIADIC_OUT {
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord;
#endif
#if HAS_COLOR_ALPHA_ATTRIBUTE
    float colorAlpha;
#endif
} variadic_out;
#endif

layout (set = 0, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 0, binding = 1, std430) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (set = 1, binding = 0, std430) readonly buffer NodeBuffer {
    Node nodes[];
};

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
} pc;

// --------------------
// Functions.
// --------------------

float getFloat(uint64_t address) {
    return FloatRef(address).data;
}

vec3 getVec3(uint64_t address){
    return Vec3Ref(address).data;
}

#if HAS_BASE_COLOR_TEXTURE
vec2 getVec2(uint64_t address){
    return Vec2Ref(address).data;
}

vec2 getTexcoord(uint texcoordIndex){
    IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.texcoordAttributeMappingInfos.data[texcoordIndex];
    return getVec2(mappingInfo.bytesPtr + int(mappingInfo.stride) * gl_VertexIndex);
}
#endif

void main(){
    outNodeIndex = NODE_INDEX;
    outMaterialIndex = MATERIAL_INDEX;
#if HAS_BASE_COLOR_TEXTURE
    variadic_out.baseColorTexcoord = getTexcoord(uint(MATERIAL.baseColorTexcoordIndex));
#endif
#if HAS_COLOR_ALPHA_ATTRIBUTE
    variadic_out.colorAlpha = getFloat(PRIMITIVE.pColorBuffer + int(PRIMITIVE.colorByteStride) * gl_VertexIndex + 12);
#endif

    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + int(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    gl_Position = pc.projectionView * TRANSFORM * vec4(inPosition, 1.0);
}