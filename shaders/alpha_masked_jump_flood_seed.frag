#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec2 fragBaseColorTexcoord;
layout (location = 1) in float baseColorAlphaFactor;
layout (location = 2) flat in int baseColorTextureIndex;
layout (location = 3) flat in float alphaCutoff;

layout (location = 0) out uvec2 outCoordinate;

layout (set = 1, binding = 0) uniform sampler2D textures[];

void main(){
    float baseColorAlpha = baseColorAlphaFactor * texture(textures[baseColorTextureIndex + 1], fragBaseColorTexcoord).a;
    if (baseColorAlpha < alphaCutoff) discard;

    outCoordinate = uvec2(gl_FragCoord.xy);
}