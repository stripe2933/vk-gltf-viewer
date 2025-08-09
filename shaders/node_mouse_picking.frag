#version 450
#if KHR_SHADER_ATOMIC_INT64 == 1
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#endif

layout (location = 0) flat in uint inNodeIndex;

layout (set = 1, binding = 0) buffer MousePickingResultBuffer {
#if KHR_SHADER_ATOMIC_INT64 == 1
    uint64_t packedNodeIndexAndDepth;
#else
    uint packedNodeIndexAndDepth;
#endif
};

void main(){
#if KHR_SHADER_ATOMIC_INT64 == 1
    atomicMax(packedNodeIndexAndDepth, packUint2x32(uvec2(inNodeIndex, floatBitsToUint(gl_FragCoord.z))));
#else
    atomicMax(packedNodeIndexAndDepth, (uint(gl_FragCoord.z * 65535) << 16) | inNodeIndex);
#endif
}