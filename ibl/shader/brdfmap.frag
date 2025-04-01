#version 450
#extension GL_GOOGLE_include_directive : require

#include "pbr.glsl"

const float PI = 3.1415926535;
const float INV_PI = 1.0 / PI;

layout (constant_id = 0) const uint TOTAL_SAMPLE_COUNT = 1024;

layout (location = 0) out vec2 outAB;

layout (push_constant, std430) uniform PushConstant {
    float framebufferWidthRcp;
    float framebufferHeightRcp;
} pc;

void main(){
    float dotNV = pc.framebufferWidthRcp * gl_FragCoord.x;
    float roughness = pc.framebufferHeightRcp * gl_FragCoord.y;

    vec3 V = vec3(sqrt(1.0 - dotNV * dotNV), 0.0, dotNV);

    const vec3 N = vec3(0.0, 0.0, 1.0);
    float A = 0.0;
    float B = 0.0;
    for (uint i = 0; i < TOTAL_SAMPLE_COUNT; ++i) {
        // generate sample vector towards the alignment of the specular lobe
        vec2 Xi = hammersleySequence(i, TOTAL_SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(Xi, N, roughness);
        float dotHV = dot(H, V);
        vec3 L  = normalize(2.0 * dotHV * H - V);

        float dotNL = max(L.z, 0.0);
        if (dotNL > 0.0) {
            dotHV = max(dotHV, 0.0);
            float dotNH = max(H.z, 0.0);
            // Geometric Shadowing term
            float G = schlickSmithGGXIbl(dotNL, dotNV, roughness);
            float G_Vis = (G * dotHV) / (dotNH * dotNV + 1e-4);
            float Fc = pow(1.0 - dotHV, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= TOTAL_SAMPLE_COUNT;
    B /= TOTAL_SAMPLE_COUNT;

    outAB = vec2(A, B);
}