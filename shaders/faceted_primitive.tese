#version 450

layout (location = 0) in vec3 inPosition[];
layout (location = 1) in vec2 inBaseColorTexcoord[];
layout (location = 2) in vec2 inMetallicRoughnessTexcoord[];
layout (location = 3) in vec2 inNormalTexcoord[];
layout (location = 4) in vec2 inOcclusionTexcoord[];
layout (location = 5) in vec2 inEmissiveTexcoord[];
layout (location = 6) patch in int inMaterialIndex;
layout (location = 7) patch in mat3 inTBN;

layout (location = 0) out vec3 outPosition;
layout (location = 1) out mat3 outTBN;
layout (location = 4) out vec2 outBaseColorTexcoord;
layout (location = 5) out vec2 outMetallicRoughnessTexcoord;
layout (location = 6) out vec2 outNormalTexcoord;
layout (location = 7) out vec2 outOcclusionTexcoord;
layout (location = 8) out vec2 outEmissiveTexcoord;
layout (location = 9) flat out int outMaterialIndex;

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
    vec3 viewPosition;
} pc;

layout (triangles, equal_spacing, cw) in;

void main() {
    outPosition = mat3(inPosition[0], inPosition[1], inPosition[2]) * gl_TessCoord; 
    outTBN = inTBN;
    outBaseColorTexcoord = mat3x2(inBaseColorTexcoord[0], inBaseColorTexcoord[1], inBaseColorTexcoord[2]) * gl_TessCoord;
    outMetallicRoughnessTexcoord = mat3x2(inMetallicRoughnessTexcoord[0], inMetallicRoughnessTexcoord[1], inMetallicRoughnessTexcoord[2]) * gl_TessCoord;
    outNormalTexcoord = mat3x2(inNormalTexcoord[0], inNormalTexcoord[1], inNormalTexcoord[2]) * gl_TessCoord;
    outOcclusionTexcoord = mat3x2(inOcclusionTexcoord[0], inOcclusionTexcoord[1], inOcclusionTexcoord[2]) * gl_TessCoord;
    outEmissiveTexcoord = mat3x2(inEmissiveTexcoord[0], inEmissiveTexcoord[1], inEmissiveTexcoord[2]) * gl_TessCoord;
    outMaterialIndex = inMaterialIndex;

    gl_Position = pc.projectionView * vec4(outPosition, 1.0);
}