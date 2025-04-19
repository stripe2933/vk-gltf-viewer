#ifndef TRANSFORM_GLSL
#define TRANSFORM_GLSL

#include "vertex_pulling.glsl"

#ifdef VERTEX_SHADER

mat4 getTransform(uint skinAttributeCount) {
    if (skinAttributeCount == 0U) {
        return instancedTransforms[NODE.instancedTransformStartIndex + gl_InstanceIndex - gl_BaseInstance];
    }
    else {
        mat4 skinMatrix = mat4(0.0);
        for (uint i = 0; i < skinAttributeCount; ++i) {
            uvec4 jointIndices = getJoints(i) + NODE.skinJointStartIndex;
            vec4 weights = getWeights(i);
            skinMatrix += weights.x * nodes[skinJointIndices[jointIndices.x]].worldTransform * inverseBindMatrices[jointIndices.x]
                        + weights.y * nodes[skinJointIndices[jointIndices.y]].worldTransform * inverseBindMatrices[jointIndices.y]
                        + weights.z * nodes[skinJointIndices[jointIndices.z]].worldTransform * inverseBindMatrices[jointIndices.z]
                        + weights.w * nodes[skinJointIndices[jointIndices.w]].worldTransform * inverseBindMatrices[jointIndices.w];
        }
        return skinMatrix;
    }
}

#endif

#endif