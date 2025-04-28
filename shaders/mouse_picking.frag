#version 450

layout (set = 0, binding = 0, input_attachment_index = 0) uniform usubpassInput inputNodeIndex;
layout (set = 0, binding = 1, std430) writeonly buffer ResultBuffer {
    uint nodeIndex;
};

void main() {
    // This shader is expected to be executed only once (scissor=1x1), therefore synchronization (such as atomic operations) is not needed.
    nodeIndex = subpassLoad(inputNodeIndex).r;
}