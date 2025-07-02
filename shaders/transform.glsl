#ifndef TRANSFORM_GLSL
#define TRANSFORM_GLSL

#include "vertex_pulling.glsl"

#ifdef VERTEX_SHADER

mat4 getTransform(uint skinAttributeCount) {
    if (skinAttributeCount == 0U) {
        return NODE.instancedWorldTransforms.data[gl_InstanceIndex - gl_BaseInstance];
    }
    else {
        mat4 skinMatrix = mat4(0.0);
        for (uint i = 0; i < skinAttributeCount; ++i) {
            uvec4 jointIndices = getJoints(i);
            vec4 weights = getWeights(i);
            skinMatrix += weights.x * nodes[NODE.skinJointIndices.data[jointIndices.x]].worldTransform * NODE.inverseBindMatrices.data[jointIndices.x]
                        + weights.y * nodes[NODE.skinJointIndices.data[jointIndices.y]].worldTransform * NODE.inverseBindMatrices.data[jointIndices.y]
                        + weights.z * nodes[NODE.skinJointIndices.data[jointIndices.z]].worldTransform * NODE.inverseBindMatrices.data[jointIndices.z]
                        + weights.w * nodes[NODE.skinJointIndices.data[jointIndices.w]].worldTransform * NODE.inverseBindMatrices.data[jointIndices.w];
        }
        return skinMatrix;
    }
}

#endif

#endif