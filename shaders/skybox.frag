#version 460

layout (location = 0) in vec3 inPosition;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform samplerCube cubemapSampler;

void main() {
    vec4 color = textureLod(cubemapSampler, inPosition, 0.0);

    // As inverse alpha blending (src and dst color is swapped) does not support the pre-multiplying alpha via
    // VkPipelineColorBlendAttachmentState, it has to be done manually.
    outColor = vec4(color.rgb * color.a, color.a);
}