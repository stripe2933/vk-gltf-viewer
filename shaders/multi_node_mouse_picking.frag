#version 450
#extension GL_KHR_shader_subgroup_vote : require
#extension GL_KHR_shader_subgroup_arithmetic : require

layout (location = 0) flat in uint inNodeIndex;

layout (set = 1, binding = 0, std430) buffer MousePickingResultBuffer {
    uint packedBits[];
};

void main() {
    uint blockIndex = inNodeIndex >> 5U;
    uint bitmask = 1U << (inNodeIndex & 31U);

    if (subgroupAllEqual(blockIndex)) {
        bitmask = subgroupOr(bitmask);
        if (subgroupElect()) {
            atomicOr(packedBits[blockIndex], bitmask);
        }
    }
    else {
        atomicOr(packedBits[blockIndex], bitmask);
    }
}