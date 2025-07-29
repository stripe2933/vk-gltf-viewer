#version 450
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout (location = 0) flat in uint inNodeIndex;

layout (set = 2, binding = 0) buffer MousePickingResultBuffer {
    uint64_t depthNodeIndexPacked;
};

void main(){
    uint intDepth = floatBitsToUint(gl_FragCoord.z);
    atomicMax(depthNodeIndexPacked, packUint2x32(uvec2(inNodeIndex, intDepth)));
}