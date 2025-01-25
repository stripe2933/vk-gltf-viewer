#ifndef INDEXING_GLSL
#define INDEXING_GLSL

#if defined(VERTEX_SHADER)

// --------------------
// Indexing macros that are used in vertex shader.
// --------------------

#define PRIMITIVE_INDEX gl_BaseInstance & 0xFFFFU
#define PRIMITIVE primitives[PRIMITIVE_INDEX]
#define NODE_INDEX gl_BaseInstance >> 16U
#define TRANSFORM nodes[NODE_INDEX].instanceTransforms.data[gl_InstanceIndex - gl_BaseInstance]
#define MATERIAL_INDEX PRIMITIVE.materialIndex
#define MATERIAL materials[MATERIAL_INDEX]

#elif defined(FRAGMENT_SHADER)

// --------------------
// Indexing macros that are used in fragment shader.
// --------------------

#define MATERIAL_INDEX inMaterialIndex
#define MATERIAL materials[inMaterialIndex]

#endif

#endif