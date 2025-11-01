#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_buffer_reference2 : require

#define VERTEX_SHADER
#include "indexing.glsl"
#include "types.glsl"

#define HAS_VARIADIC_OUT HAS_BASE_COLOR_TEXTURE || HAS_COLOR_0_ATTRIBUTE

layout (constant_id = 0) const uint POSITION_COMPONENT_TYPE = 0;
layout (constant_id = 1) const bool POSITION_NORMALIZED = false;
layout (constant_id = 2) const uint BASE_COLOR_TEXCOORD_COMPONENT_TYPE = 0;
layout (constant_id = 3) const bool BASE_COLOR_TEXCOORD_NORMALIZED = false;
layout (constant_id = 4) const uint COLOR_0_COMPONENT_TYPE = 0;
layout (constant_id = 5) const uint COLOR_0_COMPONENT_COUNT = 0;
layout (constant_id = 6) const uint POSITION_MORPH_TARGET_COUNT = 0;
layout (constant_id = 7) const uint SKIN_ATTRIBUTE_COUNT = 0;

layout (location = 0) flat out uint outMaterialIndex;
#if HAS_VARIADIC_OUT
layout (location = 1) out VS_VARIADIC_OUT {
#if HAS_BASE_COLOR_TEXTURE
    vec2 baseColorTexcoord;
#endif

#if HAS_COLOR_0_ATTRIBUTE
    vec4 color0;
#endif
} variadic_out;
#endif

layout (set = 0, binding = 0) uniform CameraBuffer {
    mat4 projectionViews[4];
} camera;

layout (set = 2, binding = 0, std430) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 2, binding = 1, std430) readonly buffer NodeBuffer {
    Node nodes[];
};
layout (set = 2, binding = 2, std430) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (push_constant) uniform PushConstant {
    uint viewIndex;
} pc;

#include "vertex_pulling.glsl"
#include "transform.glsl"

void main(){
    vec3 inPosition = getPosition(POSITION_COMPONENT_TYPE, POSITION_NORMALIZED, POSITION_MORPH_TARGET_COUNT);

    outMaterialIndex = MATERIAL_INDEX;
#if HAS_BASE_COLOR_TEXTURE
    variadic_out.baseColorTexcoord = getTexcoord(uint(MATERIAL.baseColorTexcoordIndex), BASE_COLOR_TEXCOORD_COMPONENT_TYPE, BASE_COLOR_TEXCOORD_NORMALIZED);
#endif

#if HAS_COLOR_0_ATTRIBUTE
    variadic_out.color0 = getColor0(COLOR_0_COMPONENT_TYPE, COLOR_0_COMPONENT_COUNT);
#endif

    gl_Position = camera.projectionViews[pc.viewIndex] * getTransform(SKIN_ATTRIBUTE_COUNT) * vec4(inPosition, 1.0);
    gl_PointSize = 1.0;
}