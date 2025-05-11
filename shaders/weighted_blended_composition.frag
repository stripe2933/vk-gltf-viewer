#version 450
#if AMD_SHADER_TRINARY_MINMAX == 1
#extension GL_AMD_shader_trinary_minmax : enable
#endif

const float EPSILON = 1e-5f;

layout (location = 0) out vec4 outColor;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputAccumulation;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput inputRevealage;

// --------------------
// Functions.
// --------------------

bool isApproximatelyEqual(float a, float b) {
    return abs(a - b) <= (abs(a) < abs(b) ? abs(b) : abs(a)) * EPSILON;
}

float trinaryMax(vec3 v) {
#if AMD_SHADER_TRINARY_MINMAX == 1
    return max3(v.x, v.y, v.z);
#else
    return max(max(v.x, v.y), v.z);
#endif
}

void main(){
    float revealage = subpassLoad(inputRevealage).r;
    if (isApproximatelyEqual(revealage, 1.0f)){
        discard;
    }

    vec4 accumulation = subpassLoad(inputAccumulation);
    if (isinf(trinaryMax(abs(accumulation.rgb)))) {
        accumulation.rgb = accumulation.aaa;
    }

    vec3 averageColor = accumulation.rgb / max(accumulation.a, EPSILON);
    outColor = vec4(averageColor, 1.0 - revealage);
}