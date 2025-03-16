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
            skinMatrix += weights.x * instancedTransforms[nodes[skinJointIndices[jointIndices.x]].instancedTransformStartIndex] * inverseBindMatrices[jointIndices.x]
                        + weights.y * instancedTransforms[nodes[skinJointIndices[jointIndices.y]].instancedTransformStartIndex] * inverseBindMatrices[jointIndices.y]
                        + weights.z * instancedTransforms[nodes[skinJointIndices[jointIndices.z]].instancedTransformStartIndex] * inverseBindMatrices[jointIndices.z]
                        + weights.w * instancedTransforms[nodes[skinJointIndices[jointIndices.w]].instancedTransformStartIndex] * inverseBindMatrices[jointIndices.w];
        }
        return skinMatrix;
    }
}

#endif

#endif