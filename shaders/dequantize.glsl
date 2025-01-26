#ifndef DEQUANTIZE_GLSL
#define DEQUANTIZE_GLSL

float dequantize(uint8_t data) {
    return float(data) / 255.0;
}

float dequantize(uint16_t data) {
    return float(data) / 65536.0;
}

vec2 dequantize(u8vec2 data) {
    return vec2(data) / 255.0;
}

vec2 dequantize(u16vec2 data) {
    return vec2(data) / 65536.0;
}

vec3 dequantize(u8vec3 data) {
    return vec3(data) / 255.0;
}

vec3 dequantize(u16vec3 data) {
    return vec3(data) / 65536.0;
}

vec4 dequantize(u8vec4 data) {
    return vec4(data) / 255.0;
}

vec4 dequantize(u16vec4 data) {
    return vec4(data) / 65536.0;
}

#endif // DEQUANTIZE_GLSL