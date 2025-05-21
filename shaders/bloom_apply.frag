#version 450
#if AMD_SHADER_TRINARY_MINMAX == 1
#extension GL_AMD_shader_trinary_minmax : enable
#endif

layout (location = 0) out vec4 outColor;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputResult;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputBloom;

layout (push_constant) uniform PushConstant {
    float factor;
} pc;

float trinaryMax(vec3 v) {
#if AMD_SHADER_TRINARY_MINMAX == 1
    return max3(v.x, v.y, v.z);
#else
    return max(max(v.x, v.y), v.z);
#endif
}

vec3 tonemap(vec3 color) {
    return color / (1.0 + trinaryMax(color));
}

vec3 tonemapInvert(vec3 color) {
    // If color ≥ 509 is tone mapped, 509 / (1 + 509) = 0.998 ≥ 254.5 / 255, which will be stored as 1 for 8-bit float
    // precision. This will cause division by 0, make infinite value, propagated to the whole image by the bloom pass.
    // To avoid this, we need to clamp the inverted tone mapped value to 509.
    return min(color / (1.0 - trinaryMax(color)), vec3(509.0));
}

void main() {
    vec4 inputColor = subpassLoad(inputResult);
    inputColor.rgb = tonemapInvert(inputColor.rgb);
    vec4 bloomColor = subpassLoad(inputBloom) * pc.factor;

    // Alpha blending with premultiplied alpha.
    outColor.rgb = tonemap(bloomColor.rgb + inputColor.rgb * (1.0 - bloomColor.a));
    outColor.a = bloomColor.a + inputColor.a * (1.0 - bloomColor.a);
}