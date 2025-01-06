#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

#define VERTEX_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_OUT HAS_BASE_COLOR_TEXTURE || HAS_COLOR_ALPHA_ATTRIBUTE

layout (constant_id = 0) const uint TEXCOORD_COMPONENT_TYPE = 5126; // FLOAT
layout (constant_id = 1) const uint COLOR_COMPONENT_TYPE = 5126; // FLOAT

layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer Uint8Ref { uint8_t data; };
layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer U8Vec2Ref { u8vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer Uint16Ref { uint16_t data; };
layout (std430, buffer_reference, buffer_reference_align = 2) readonly buffer U16Vec2Ref { u16vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer FloatRef { float data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Vec3Ref { vec3 data; };
layout (std430, buffer_reference, buffer_reference_align = 64) readonly buffer Node { mat4 transforms[]; };

layout (location = 0) flat out uint outMaterialIndex;
#if HAS_VARIADIC_OUT
layout (location = 1) out VS_VARIADIC_OUT {
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

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
} pc;

// --------------------
// Functions.
// --------------------

vec3 getPosition() {
    return Vec3Ref(PRIMITIVE.pPositionBuffer + int(PRIMITIVE.positionByteStride) * gl_VertexIndex).data;
}

#if HAS_BASE_COLOR_TEXTURE
vec2 getTexcoord(uint texcoordIndex){
    IndexedAttributeMappingInfo mappingInfo = PRIMITIVE.texcoordAttributeMappingInfos.data[texcoordIndex];
    uint64_t fetchAddress = mappingInfo.bytesPtr + int(mappingInfo.stride) * gl_VertexIndex;

    switch (TEXCOORD_COMPONENT_TYPE) {
    case 5121U: // UNSIGNED BYTE
        return vec2(U8Vec2Ref(fetchAddress).data) / 255.0;
    case 5123U: // UNSIGNED SHORT
        return vec2(U16Vec2Ref(fetchAddress).data) / 65535.0;
    case 5126U: // FLOAT
        return Vec2Ref(fetchAddress).data;
    }
    return vec2(0.0);
}
#endif

#if HAS_COLOR_ALPHA_ATTRIBUTE
float getColorAlpha() {
    switch (COLOR_COMPONENT_TYPE) {
    case 5121U: // UNSIGNED BYTE
        return float(Uint8Ref(PRIMITIVE.pColorBuffer + int(PRIMITIVE.colorByteStride) * gl_VertexIndex + 3).data) / 255.0;
    case 5123U: // UNSIGNED SHORT
        return float(Uint16Ref(PRIMITIVE.pColorBuffer + int(PRIMITIVE.colorByteStride) * gl_VertexIndex + 6).data) / 65535.0;
    case 5126U: // FLOAT
        return FloatRef(PRIMITIVE.pColorBuffer + int(PRIMITIVE.colorByteStride) * gl_VertexIndex + 12).data;
    }
    return 1.0;
}
#endif

void main(){
    outMaterialIndex = MATERIAL_INDEX;
#if HAS_BASE_COLOR_TEXTURE
    variadic_out.baseColorTexcoord = getTexcoord(uint(MATERIAL.baseColorTexcoordIndex));
#endif
#if HAS_COLOR_ALPHA_ATTRIBUTE
    variadic_out.colorAlpha = getColorAlpha();
#endif

    vec3 inPosition = getPosition();
    gl_Position = pc.projectionView * TRANSFORM * vec4(inPosition, 1.0);
}