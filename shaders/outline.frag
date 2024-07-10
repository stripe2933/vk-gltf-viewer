#version 460

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0, rg16ui) uniform readonly uimage2D jumpFloodImage;

layout (push_constant, std430) uniform PushConstant {
    vec4 outlineColor;
    ivec2 passthruOffset;
    float outlineThickness;
} pc;

void main(){
    ivec2 sampleCoord = ivec2(gl_FragCoord.xy) - pc.passthruOffset;
    float signedDistance = distance(imageLoad(jumpFloodImage, sampleCoord).xy, sampleCoord);
    outColor = pc.outlineColor;
    outColor.a = (signedDistance > 1.0 ? outColor.a * smoothstep(pc.outlineThickness + 1.0, pc.outlineThickness, signedDistance) : 0.0);
}