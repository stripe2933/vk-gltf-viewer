#version 460

layout (location = 0) flat in uint primitiveNodeIndex;

layout (location = 0) out uint outNodeIndex;
layout (location = 1) out uvec2 hoveringNodeJumpFloodCoord;
layout (location = 2) out uvec2 selectedNodeJumpFloodCoord;

layout (push_constant, std430) uniform PushConstant {
    layout (offset = 64)
    uint hoveringNodeIndex;
    uint selectedNodeIndex;
} pc;

layout (early_fragment_tests) in;

void main(){
    outNodeIndex = primitiveNodeIndex;
    if (outNodeIndex == pc.hoveringNodeIndex){
        hoveringNodeJumpFloodCoord = uvec2(gl_FragCoord.xy);
    }
    if (outNodeIndex == pc.selectedNodeIndex){
        selectedNodeJumpFloodCoord = uvec2(gl_FragCoord.xy);
    }
}