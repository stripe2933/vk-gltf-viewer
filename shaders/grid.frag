#version 450

layout (location = 0) in vec2 inUV;
layout (location = 1) flat in float inLogLodFract;

layout (location = 0) out vec4 outColor;

layout (push_constant, std430) uniform PushConstants {
    vec3 color;
    bool showMinorAxes;
} pc;

float pristineGrid(vec2 uv, vec2 lineWidth) {
    vec2 ddx = dFdx(uv);
    vec2 ddy = dFdy(uv);
    vec2 uvDeriv = vec2(length(vec2(ddx.x, ddy.x)), length(vec2(ddx.y, ddy.y)));
    bvec2 invertLine = bvec2(lineWidth.x > 0.5, lineWidth.y > 0.5);
    vec2 targetWidth = mix(lineWidth, 1.0 - lineWidth, invertLine);
    vec2 drawWidth = clamp(targetWidth, uvDeriv, vec2(0.5));
    vec2 lineAA = uvDeriv * 1.5;
    vec2 gridUV = abs(2.0 * fract(uv) - 1.0);
    gridUV = mix(1.0 - gridUV, gridUV, invertLine);
    vec2 grid2 = smoothstep(drawWidth + lineAA, drawWidth - lineAA, gridUV);

    grid2 *= clamp(targetWidth / drawWidth, 0.0, 1.0);
    grid2 = mix(grid2, targetWidth, clamp(2.0 * uvDeriv - 1.0, 0.0, 1.0));
    grid2 = mix(grid2, 1.0 - grid2, invertLine);
    return mix(grid2.x, 1.0, grid2.y);
}

void main() {
    vec2 lineWidth = vec2(0.008);
    outColor = vec4(pc.color, pristineGrid(inUV, mix(1.0, 0.1, inLogLodFract) * lineWidth));
    if (pc.showMinorAxes) {
        outColor.a += inLogLodFract * pristineGrid(10.0 * inUV, mix(2.5, 1.0, inLogLodFract) * lineWidth);
    }
}