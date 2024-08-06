#version 460
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

// For convinience.
#define PRIMITIVE primitives[gl_InstanceIndex]
#define MATERIAL materials[PRIMITIVE.materialIndex]
#define TRANSFORM nodeTransforms[PRIMITIVE.nodeIndex]

layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer Ubytes { uint8_t data[]; };
layout (std430, buffer_reference, buffer_reference_align = 8) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 16) readonly buffer Vec4Ref { vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 8) readonly buffer Pointers { uint64_t data[]; };

struct Material {
    uint8_t baseColorTexcoordIndex;
    uint8_t metallicRoughnessTexcoordIndex;
    uint8_t normalTexcoordIndex;
    uint8_t occlusionTexcoordIndex;
    uint8_t emissiveTexcoordIndex;
    uint8_t padding0[1];
    int16_t baseColorTextureIndex;
    int16_t metallicRoughnessTextureIndex;
    int16_t normalTextureIndex;
    int16_t occlusionTextureIndex;
    int16_t emissiveTextureIndex;
    uint8_t FRAGMENT_DATA[48];
};

struct Primitive {
    uint64_t pPositionBuffer;
    uint64_t pNormalBuffer;
    uint64_t pTangentBuffer;
    Pointers texcoordBufferPtrs;
    Pointers colorBufferPtrs;
    uint8_t positionByteStride;
    uint8_t normalByteStride;
    uint8_t tangentByteStride;
    uint8_t padding[5];
    Ubytes texcoordByteStrides;
    Ubytes colorByteStrides;
    uint nodeIndex;
    uint materialIndex;
};

layout (location = 0) out vec3 fragPosition;
layout (location = 1) out mat3 fragTBN;
layout (location = 4) out vec2 fragBaseColorTexcoord;
layout (location = 5) out vec2 fragMetallicRoughnessTexcoord;
layout (location = 6) out vec2 fragNormalTexcoord;
layout (location = 7) out vec2 fragOcclusionTexcoord;
layout (location = 8) out vec2 fragEmissiveTexcoord;
layout (location = 9) flat out uint materialIndex;

layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (set = 2, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 2, binding = 1) readonly buffer NodeTransformBuffer {
    mat4 nodeTransforms[];
};

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
    vec3 viewPosition;
} pc;

// --------------------
// Functions.
// --------------------

vec2 getVec2(uint64_t address){
    return Vec2Ref(address).data;
}

vec3 getVec3(uint64_t address){
    return Vec4Ref(address).data.xyz;
}

vec4 getVec4(uint64_t address){
    return Vec4Ref(address).data;
}

vec2 getTexcoord(uint texcoordIndex){
    return getVec2(PRIMITIVE.texcoordBufferPtrs.data[texcoordIndex] + uint(PRIMITIVE.texcoordByteStrides.data[texcoordIndex]) * gl_VertexIndex);
}

void main(){
    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    vec3 inNormal = getVec3(PRIMITIVE.pNormalBuffer + uint(PRIMITIVE.normalByteStride) * gl_VertexIndex);

    mat4 transform = TRANSFORM;
    fragPosition = (transform * vec4(inPosition, 1.0)).xyz;
    fragTBN[2] = normalize(mat3(transform) * inNormal); // N

    if (int(MATERIAL.baseColorTextureIndex) != -1){
        fragBaseColorTexcoord = getTexcoord(uint(MATERIAL.baseColorTexcoordIndex));
    }
    if (int(MATERIAL.metallicRoughnessTextureIndex) != -1){
        fragMetallicRoughnessTexcoord = getTexcoord(uint(MATERIAL.metallicRoughnessTexcoordIndex));
    }
    if (int(MATERIAL.normalTextureIndex) != -1){
        vec4 inTangent = getVec4(PRIMITIVE.pTangentBuffer + uint(PRIMITIVE.tangentByteStride) * gl_VertexIndex);
        fragTBN[0] = normalize(mat3(transform) * inTangent.xyz); // T
        fragTBN[1] = cross(fragTBN[2], fragTBN[0]) * -inTangent.w; // B

        fragNormalTexcoord = getTexcoord(uint(MATERIAL.normalTexcoordIndex));
    }
    if (int(MATERIAL.occlusionTextureIndex) != -1){
        fragOcclusionTexcoord = getTexcoord(uint(MATERIAL.occlusionTexcoordIndex));
    }
    if (int(MATERIAL.emissiveTextureIndex) != -1){
        fragEmissiveTexcoord = getTexcoord(uint(MATERIAL.emissiveTexcoordIndex));
    }
    materialIndex = PRIMITIVE.materialIndex;

    gl_Position = pc.projectionView * vec4(fragPosition, 1.0);
}