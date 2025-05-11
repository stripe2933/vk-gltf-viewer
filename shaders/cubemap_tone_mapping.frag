#version 450
#extension GL_EXT_multiview : require
#extension GL_EXT_samplerless_texture_functions : require
#if AMD_SHADER_TRINARY_MINMAX == 1
#extension GL_AMD_shader_trinary_minmax : enable
#endif

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform texture2DArray highPrecisionCubemapImageArray;

float trinaryMax(vec3 v) {
#if AMD_SHADER_TRINARY_MINMAX == 1
    return max3(v.x, v.y, v.z);
#else
    return max(max(v.x, v.y), v.z);
#endif
}

void main() {
    vec3 color = texelFetch(highPrecisionCubemapImageArray, ivec3(gl_FragCoord.xy, gl_ViewIndex), 0).rgb;
    outColor = vec4(color / (1.0 + trinaryMax(color)), 1.0);
}