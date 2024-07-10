#version 450

#extension GL_EXT_scalar_block_layout : require

struct SphericalHarmonicBasis{
    float band0[1];
    float band1[3];
    float band2[5];
};

layout (location = 0) in vec3 fragPosition;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0, scalar) uniform SphericalHarmonicsBuffer {
    vec3 coefficients[9];
} sphericalHarmonics;

layout (early_fragment_tests) in;

// --------------------
// Functions.
// --------------------

SphericalHarmonicBasis getSphericalHarmonicBasis(vec3 v){
    return SphericalHarmonicBasis(
    float[1](0.282095),
    float[3](-0.488603 * v.y, 0.488603 * v.z, -0.488603 * v.x),
    float[5](1.092548 * v.x * v.y, -1.092548 * v.y * v.z, 0.315392 * (3.0 * v.z * v.z - 1.0), -1.092548 * v.x * v.z, 0.546274 * (v.x * v.x - v.y * v.y))
    );
}

vec3 computeDiffuseIrradiance(vec3 normal){
    SphericalHarmonicBasis basis = getSphericalHarmonicBasis(normal);
    vec3 irradiance
    = 3.141593 * (sphericalHarmonics.coefficients[0] * basis.band0[0])
    + 2.094395 * (sphericalHarmonics.coefficients[1] * basis.band1[0]
    +  sphericalHarmonics.coefficients[2] * basis.band1[1]
    +  sphericalHarmonics.coefficients[3] * basis.band1[2])
    + 0.785398 * (sphericalHarmonics.coefficients[4] * basis.band2[0]
    +  sphericalHarmonics.coefficients[5] * basis.band2[1]
    +  sphericalHarmonics.coefficients[6] * basis.band2[2]
    +  sphericalHarmonics.coefficients[7] * basis.band2[3]
    +  sphericalHarmonics.coefficients[8] * basis.band2[4]);
    return irradiance / 3.141593;
}

void main() {
    outColor = vec4(computeDiffuseIrradiance(normalize(fragPosition)), 1.0);
}