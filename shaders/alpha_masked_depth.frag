#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec2 fragBaseColorTexcoord;
layout (location = 1) flat in uint nodeIndex;
layout (location = 2) in float baseColorAlphaFactor;
layout (location = 3) flat in int baseColorTextureIndex;
layout (location = 4) flat in float alphaCutoff;

layout (location = 0) out uint outNodeIndex;

layout (set = 1, binding = 0) uniform sampler2D textures[];

void main(){
    float baseColorAlpha = baseColorAlphaFactor * texture(textures[baseColorTextureIndex + 1], fragBaseColorTexcoord).a;
    if (baseColorAlpha < alphaCutoff) discard;

    outNodeIndex = nodeIndex;
}