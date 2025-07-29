#version 450
#extension GL_EXT_multiview : require
#extension GL_EXT_samplerless_texture_functions : require

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform utexture2DArray jumpFloodImages;

layout (push_constant) uniform PushConstant {
    vec4 outlineColor;
    float outlineThickness;
} pc;

void main(){
    ivec3 sampleCoord = ivec3(gl_FragCoord.xy, gl_ViewIndex);
    float signedDistance = distance(texelFetch(jumpFloodImages, sampleCoord, 0).xy, sampleCoord.xy);
    outColor = pc.outlineColor;
    outColor.a = (signedDistance > 1.0 ? outColor.a * smoothstep(pc.outlineThickness + 1.0, pc.outlineThickness, signedDistance) : 0.0);
}