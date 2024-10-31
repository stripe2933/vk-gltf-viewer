#version 460

layout (location = 0) flat in uint inNodeIndex;

layout (location = 0) out uint outNodeIndex;

layout (early_fragment_tests) in;

void main(){
    outNodeIndex = inNodeIndex;
}