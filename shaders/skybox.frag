#version 460

layout (location = 0) in vec3 fragPosition;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform samplerCube cubemapSampler;

layout (early_fragment_tests) in;

void main() {
    outColor = vec4(textureLod(cubemapSampler, fragPosition, 0.0).rgb, 1.0);
}