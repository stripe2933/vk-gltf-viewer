#ifndef DEQUANTIZE_GLSL
#define DEQUANTIZE_GLSL

float dequantize(int8_t data) {
    return max(float(data) / 127.0, -1.0);
}

float dequantize(uint8_t data) {
    return float(data) / 255.0;
}

float dequantize(int16_t data) {
    return max(float(data) / 32767.0, -1.0);
}

float dequantize(uint16_t data) {
    return float(data) / 65536.0;
}

vec2 dequantize(i8vec2 data) {
    return max(vec2(data) / 127.0, -1.0);
}

vec2 dequantize(u8vec2 data) {
    return vec2(data) / 255.0;
}

vec3 dequantize(i8vec3 data) {
    return max(vec3(data) / 127.0, -1.0);
}

vec3 dequantize(u8vec3 data) {
    return vec3(data) / 255.0;
}

vec3 dequantize(i16vec3 data) {
    return max(vec3(data) / 32767.0, -1.0);
}

vec3 dequantize(u16vec3 data) {
    return vec3(data) / 65536.0;
}

#endif // DEQUANTIZE_GLSL