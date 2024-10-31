#if defined(VERTEX_SHADER)
#define PRIMITIVE_INDEX gl_BaseInstance & 0xFFFFU
#define PRIMITIVE primitives[PRIMITIVE_INDEX]
#define NODE_INDEX gl_BaseInstance >> 16U
#define TRANSFORM nodeTransforms[NODE_INDEX]
#define MATERIAL_INDEX PRIMITIVE.materialIndex
#define MATERIAL materials[MATERIAL_INDEX + 1]
#elif defined(FRAGMENT_SHADER)
#define MATERIAL_INDEX inMaterialIndex
#define MATERIAL materials[inMaterialIndex + 1]
#endif