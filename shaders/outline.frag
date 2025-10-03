#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform utexture2DArray jumpFloodImage;

layout (push_constant) uniform PushConstant {
    vec4 outlineColor;
    float outlineThickness;
} pc;

void main(){
    ivec3 fetchCoord = ivec3(ivec2(gl_FragCoord.xy), 0);

    // Determining the fetch coordinate:
    //
    // Currently the application supports view count of 1, 2 and 4. For view count of 1, the jump flood image extent is the
    // same as the framebuffer extent. For view count of 2, the framebuffer extent width is doubled. For view count of 4,
    // both the framebuffer extent width and height are doubled.
    //
    // Therefore, the fetch coordinate can be determined by the following rule:
    // - if gl_FragCoord.x >= jumpFloodImageSize.x, (fetch coordinate x) = (gl_FragCoord.x - jumpFloodImageSize.x), and the layer bit 0 is set.
    // - if gl_FragCoord.y >= jumpFloodImageSize.y, (fetch coordinate y) = (gl_FragCoord.y - jumpFloodImageSize.y), and the layer bit 1 is set.
    // The result layer index will be the bitwise OR of the two bits.

    ivec2 jumpFloodImageSize = textureSize(jumpFloodImage, 0).xy;
    if (fetchCoord.x >= jumpFloodImageSize.x) {
        fetchCoord.x -= jumpFloodImageSize.x;
        fetchCoord.z |= 1;
    }
    if (fetchCoord.y >= jumpFloodImageSize.y) {
        fetchCoord.y -= jumpFloodImageSize.y;
        fetchCoord.z |= 2;
    }

    float signedDistance = distance(texelFetch(jumpFloodImage, fetchCoord, 0).xy, fetchCoord.xy);
    outColor = pc.outlineColor;
    outColor.a = (signedDistance > 1.0 ? outColor.a * smoothstep(pc.outlineThickness + 1.0, pc.outlineThickness, signedDistance) : 0.0);
}