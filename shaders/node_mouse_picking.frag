#version 450

layout (location = 0) flat in uint inNodeIndex;

layout (set = 1, binding = 0) buffer MousePickingResultBuffer {
    uint packedNodeIndexAndDepth;
};

layout (early_fragment_tests) in;

void main(){
    atomicMax(packedNodeIndexAndDepth, (uint(gl_FragCoord.z * 65535) << 16) | inNodeIndex);
}