#version 460
#extension GL_EXT_scalar_block_layout : require

#include "spherical_harmonics.glsl"

layout (location = 0) in vec3 fragPosition;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0, scalar) uniform SphericalHarmonicsBuffer {
    vec3 coefficients[9];
} sphericalHarmonics;
/*layout (set = 0, binding = 1) uniform samplerCube prefilteredmap;
layout (set = 0, binding = 2) uniform sampler2D brdfmap;*/

layout (early_fragment_tests) in;

// --------------------
// Functions.
// --------------------

vec3 diffuseIrradiance(vec3 normal){
    SphericalHarmonicBasis basis = SphericalHarmonicBasis_construct(normal);
    return SphericalHarmonicBasis_restore(basis, sphericalHarmonics.coefficients) / 3.141593;
}

void main() {
    outColor = vec4(diffuseIrradiance(normalize(fragPosition)), 1.0);
}