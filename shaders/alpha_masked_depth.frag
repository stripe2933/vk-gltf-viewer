#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec2 fragBaseColorTexcoord;
layout (location = 1) flat in uint primitiveNodeIndex;
layout (location = 2) in float baseColorAlphaFactor;
layout (location = 3) flat in int baseColorTextureIndex;

layout (location = 0) out uint outNodeIndex;
layout (location = 1) out uvec2 hoveringNodeJumpFloodCoord;
layout (location = 2) out uvec2 selectedNodeJumpFloodCoord;

layout (set = 1, binding = 0) uniform sampler2D textures[];

layout (push_constant, std430) uniform PushConstant {
    layout (offset = 64)
    uint hoveringNodeIndex;
    uint selectedNodeIndex;
} pc;

void main(){
    float baseColorAlpha = baseColorAlphaFactor * texture(textures[baseColorTextureIndex + 1], fragBaseColorTexcoord).a;
    if (baseColorAlpha < 0.1) discard;

    outNodeIndex = primitiveNodeIndex;
    if (outNodeIndex == pc.hoveringNodeIndex){
        hoveringNodeJumpFloodCoord = uvec2(gl_FragCoord.xy);
    }
    if (outNodeIndex == pc.selectedNodeIndex){
        selectedNodeJumpFloodCoord = uvec2(gl_FragCoord.xy);
    }
}