// Normal Distribution
float dGgx(float dotNH, float roughness){
    const float PI = 3.1415926535;

    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

// Geometric Shadowing
float schlickSmithGGX(float dotN, float k){
    return dotN / (dotN * (1.0 - k) + k);
}

float schlickSmithGGXIbl(float dotNL, float dotNV, float roughness){
    float alpha = roughness;
    float k = (alpha * alpha) / 2.0; // special remap of k for IBL lighting
    float GL = schlickSmithGGX(dotNL, k);
    float GV = schlickSmithGGX(dotNV, k);
    return GL * GV;
}

// Van Der Corpus sequence
// @see http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float vdcSequence(uint bits){
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// Hammersley sequence
// @see http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
vec2 hammersleySequence(uint i, uint N){
    return vec2(float(i) / float(N), vdcSequence(i));
}

// GGX NDF via importance sampling
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness){
    const float PI = 3.1415926535;

    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha2 - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // from spherical coordinates to cartesian coordinates
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    // from tangent-space vector to world-space sample vector
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}