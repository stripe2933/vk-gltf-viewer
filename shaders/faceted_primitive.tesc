#version 450

layout (location = 0) in vec3 inPosition[];
layout (location = 1) in vec2 inBaseColorTexcoord[];
layout (location = 2) in vec2 inMetallicRoughnessTexcoord[];
layout (location = 3) in vec2 inNormalTexcoord[];
layout (location = 4) in vec2 inOcclusionTexcoord[];
layout (location = 5) in vec2 inEmissiveTexcoord[];
layout (location = 6) in int inMaterialIndex[];

layout (location = 0) out vec3 outPosition[];
layout (location = 1) out vec2 outBaseColorTexcoord[];
layout (location = 2) out vec2 outMetallicRoughnessTexcoord[];
layout (location = 3) out vec2 outNormalTexcoord[];
layout (location = 4) out vec2 outOcclusionTexcoord[];
layout (location = 5) out vec2 outEmissiveTexcoord[];
layout (location = 6) patch out int outMaterialIndex;
layout (location = 7) patch out mat3 outTBN;

layout (vertices = 3) out;

void main() {
    outPosition[gl_InvocationID] = inPosition[gl_InvocationID];
    outBaseColorTexcoord[gl_InvocationID] = inBaseColorTexcoord[gl_InvocationID];
    outMetallicRoughnessTexcoord[gl_InvocationID] = inMetallicRoughnessTexcoord[gl_InvocationID];
    outNormalTexcoord[gl_InvocationID] = inNormalTexcoord[gl_InvocationID];
    outOcclusionTexcoord[gl_InvocationID] = inOcclusionTexcoord[gl_InvocationID];
    outEmissiveTexcoord[gl_InvocationID] = inEmissiveTexcoord[gl_InvocationID];

    if (gl_InvocationID == 0){
        gl_TessLevelInner[0] = 1.0;
        gl_TessLevelOuter[0] = 1.0;
        gl_TessLevelOuter[1] = 1.0;
        gl_TessLevelOuter[2] = 1.0;

        outMaterialIndex = inMaterialIndex[0];

        // Calculate TBN.
        vec3 e1 = inPosition[1] - inPosition[0];
        vec3 e2 = inPosition[2] - inPosition[0];
        vec2 deltaUV1 = inBaseColorTexcoord[1] - inBaseColorTexcoord[0];
        vec2 deltaUV2 = inBaseColorTexcoord[2] - inBaseColorTexcoord[0];
        mat2x3 TB = mat2x3(e1, e2) * inverse(mat2(deltaUV1, deltaUV2));
        outTBN[0] = normalize(TB[0]);
        outTBN[1] = normalize(TB[1]);
        outTBN[2] = cross(outTBN[0], outTBN[1]);
    }
}