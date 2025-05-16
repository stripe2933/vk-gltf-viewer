#version 450

layout (location = 0) out vec4 outColor;

layout (push_constant) uniform PushConstant {
    vec4 color;
} pc;

void main() {
    // As inverse alpha blending (src and dst color is swapped) does not support the pre-multiplying alpha via
    // VkPipelineColorBlendAttachmentState, it has to be done manually.
    outColor = vec4(pc.color.rgb * pc.color.a, pc.color.a);
}