#version 450
#extension GL_EXT_multiview : require
#extension GL_EXT_samplerless_texture_functions : require

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform texture2DArray highPrecisionCubemapImageArray;

void main() {
    vec3 color = texelFetch(highPrecisionCubemapImageArray, ivec3(gl_FragCoord.xy, gl_ViewIndex), 0).rgb;
    float luminance = dot(color, REC_709_LUMA);
    outColor = vec4(color / (1.0 + luminance), 1.0);
}