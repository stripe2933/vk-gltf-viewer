#version 460

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);

layout (constant_id = 0) const bool isCubemapImageToneMapped = false;

layout (location = 0) in vec3 inPosition;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform samplerCube cubemapSampler;

layout (early_fragment_tests) in;

void main() {
    vec3 color = textureLod(cubemapSampler, inPosition, 0.0).rgb;
    if (!isCubemapImageToneMapped) {
        float luminance = dot(color, REC_709_LUMA);
        color = color / (1.0 + luminance);
    }
    outColor = vec4(color, 1.0);
}