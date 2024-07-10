#version 450

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0, rgba16f) uniform readonly image2D hdrImage;

layout (push_constant) uniform PushConstant {
    ivec2 hdriImageOffset;
} pc;

void main(){
    vec4 color = imageLoad(hdrImage, ivec2(gl_FragCoord.xy) - pc.hdriImageOffset);
    float luminance = dot(color.rgb, REC_709_LUMA);
    outColor = vec4(color.rgb / (1.0 + luminance), color.a);
}