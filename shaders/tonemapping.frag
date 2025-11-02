#version 450
#if AMD_SHADER_TRINARY_MINMAX == 1
#extension GL_AMD_shader_trinary_minmax : enable
#endif

layout (location = 0) out vec4 outColor;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inColor;

float trinaryMax(vec3 v) {
#if AMD_SHADER_TRINARY_MINMAX == 1
    return max3(v.x, v.y, v.z);
#else
    return max(max(v.x, v.y), v.z);
#endif
}

void main() {
    vec3 color = subpassLoad(inColor).rgb;
    outColor = vec4(color / (1.0 + trinaryMax(color)), 1.0);
}