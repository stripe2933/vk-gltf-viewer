#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform utexture2D jumpFloodImage;

layout (push_constant) uniform PushConstant {
    vec4 outlineColor;
    float outlineThickness;
} pc;

void main(){
    ivec2 sampleCoord = ivec2(gl_FragCoord.xy);
    float signedDistance = distance(texelFetch(jumpFloodImage, sampleCoord, 0).xy, sampleCoord);
    outColor = pc.outlineColor;
    outColor.a = (signedDistance > 1.0 ? outColor.a * smoothstep(pc.outlineThickness + 1.0, pc.outlineThickness, signedDistance) : 0.0);
}