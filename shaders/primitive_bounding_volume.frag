#version 450

layout (location = 0) out vec4 outAccumulation;
layout (location = 1) out float outRevealage;

layout (push_constant) uniform PushConstant {
    layout (offset = 64) vec4 color;
} pc;

layout (early_fragment_tests) in;

void main(){
    // Weighted Blended.
    float weight = clamp(
        pow(min(1.0, pc.color.a * 10.0) + 0.01, 3.0) * 1e8 * pow(1.0 - gl_FragCoord.z * 0.9, 3.0),
        1e-2, 3e3);
    outAccumulation = vec4(pc.color.rgb * pc.color.a, pc.color.a) * weight;
    outRevealage = pc.color.a;
}