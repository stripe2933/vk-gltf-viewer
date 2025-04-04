#if defined(VERTEX_SHADER)

// --------------------
// Indexing macros that are used in vertex shader.
// --------------------

#define PRIMITIVE_INDEX gl_BaseInstance & 0xFFFFU
#define PRIMITIVE primitives[PRIMITIVE_INDEX]
#define NODE_INDEX gl_BaseInstance >> 16U
#define NODE nodes[NODE_INDEX]
#define MATERIAL_INDEX PRIMITIVE.materialIndex
#define MATERIAL materials[MATERIAL_INDEX]

#elif defined(FRAGMENT_SHADER)

// --------------------
// Indexing macros that are used in fragment shader.
// --------------------

#define MATERIAL_INDEX inMaterialIndex
#define MATERIAL materials[inMaterialIndex]

#endif