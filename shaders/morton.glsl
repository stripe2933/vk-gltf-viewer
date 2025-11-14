#ifndef MORTON_GLSL
#define MORTON_GLSL

uint unmortoner16(uint z) {
    z &= 0x55u;
    z = (z ^ (z >> 1)) & 0x33u;
    z = (z ^ (z >> 2)) & 0x0Fu;
    return z;
}

uvec2 unmorton16(uint z) {
    return uvec2(unmortoner16(z), unmortoner16(z >> 1));
}

#endif