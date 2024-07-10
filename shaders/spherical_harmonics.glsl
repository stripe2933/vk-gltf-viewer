#ifndef SPHERICAL_HARMONICS_GLSL
#define SPHERICAL_HARMONICS_GLSL

struct SphericalHarmonicBasis{
    float band0[1];
    float band1[3];
    float band2[5];
};

SphericalHarmonicBasis SphericalHarmonicBasis_construct(vec3 v){
    return SphericalHarmonicBasis(
        float[1](0.282095),
        float[3](-0.488603 * v.y, 0.488603 * v.z, -0.488603 * v.x),
        float[5](1.092548 * v.x * v.y, -1.092548 * v.y * v.z, 0.315392 * (3.0 * v.z * v.z - 1.0), -1.092548 * v.x * v.z, 0.546274 * (v.x * v.x - v.y * v.y))
    );
}

vec3 SphericalHarmonicBasis_restore(SphericalHarmonicBasis basis, vec3 coefficients[9]) {
    return 3.141593 * (coefficients[0] * basis.band0[0])
         + 2.094395 * (coefficients[1] * basis.band1[0]
             +  coefficients[2] * basis.band1[1]
             +  coefficients[3] * basis.band1[2])
         + 0.785398 * (coefficients[4] * basis.band2[0]
             +  coefficients[5] * basis.band2[1]
             +  coefficients[6] * basis.band2[2]
             +  coefficients[7] * basis.band2[3]
             +  coefficients[8] * basis.band2[4]);
}

#endif // SPHERICAL_HARMONICS_GLSL