#version 460

layout (location = 0) in vec3 inPosition;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform samplerCube cubemapSampler;

layout (early_fragment_tests) in;

void main() {
    vec3 color = textureLod(cubemapSampler, inPosition, 0.0).rgb;
    outColor = vec4(color, 1.0);
}