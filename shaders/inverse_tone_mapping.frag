#version 450
#if AMD_SHADER_TRINARY_MINMAX == 1
#extension GL_AMD_shader_trinary_minmax : enable
#endif

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputTonemapped;
layout (set = 0, binding = 1) writeonly uniform image2DArray bloomImage;

layout (early_fragment_tests) in;

float trinaryMax(vec3 v) {
#if AMD_SHADER_TRINARY_MINMAX == 1
    return max3(v.x, v.y, v.z);
#else
    return max(max(v.x, v.y), v.z);
#endif
}

vec3 tonemapInvert(vec3 color) {
    // If color ≥ 509 is tone mapped, 509 / (1 + 509) = 0.998 ≥ 254.5 / 255, which will be stored as 1 for 8-bit float
    // precision. This will cause division by 0, make infinite value, propagated to the whole image by the bloom pass.
    // To avoid this, we need to clamp the inverted tone mapped value to 509.
    return min(color / (1.0 - trinaryMax(color)), vec3(509.0));
}

void main() {
    ivec3 storeCoord = ivec3(ivec2(gl_FragCoord.xy), 0);

    // Determining the fetch coordinate:
    //
    // Currently the application supports view count of 1, 2 and 4. For view count of 1, the bloom image extent is the
    // same as the framebuffer extent. For view count of 2, the framebuffer extent width is doubled. For view count of 4,
    // both the framebuffer extent width and height are doubled.
    //
    // Therefore, the fetch coordinate can be determined by the following rule:
    // - if gl_FragCoord.x >= bloomImageSize.x, (fetch coordinate x) = (gl_FragCoord.x - bloomImageSize.x), and the layer bit 0 is set.
    // - if gl_FragCoord.y >= bloomImageSize.y, (fetch coordinate y) = (gl_FragCoord.y - bloomImageSize.y), and the layer bit 1 is set.
    // The result layer index will be the bitwise OR of the two bits.

    ivec2 bloomImageSize = imageSize(bloomImage).xy;
    if (storeCoord.x >= bloomImageSize.x) {
        storeCoord.x -= bloomImageSize.x;
        storeCoord.z |= 1;
    }
    if (storeCoord.y >= bloomImageSize.y) {
        storeCoord.y -= bloomImageSize.y;
        storeCoord.z |= 2;
    }

    vec4 tonemapped = subpassLoad(inputTonemapped);
    imageStore(bloomImage, storeCoord, vec4(tonemapInvert(tonemapped.rgb), tonemapped.a));
}