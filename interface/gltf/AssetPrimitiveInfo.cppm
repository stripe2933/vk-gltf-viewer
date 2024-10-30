export module vk_gltf_viewer:gltf.AssetPrimitiveInfo;

import std;
export import vulkan_hpp;
export import glm;

namespace vk_gltf_viewer::gltf {
    struct AssetPrimitiveInfo {
        struct IndexBufferInfo { vk::DeviceSize offset; vk::IndexType type; };
        struct AttributeBufferInfo { vk::DeviceAddress address; std::uint8_t byteStride; };
        struct IndexedAttributeBufferInfos { vk::DeviceAddress pMappingBuffer; std::vector<AttributeBufferInfo> attributeInfos; };

        std::optional<std::size_t> materialIndex;
        std::uint32_t drawCount;
        std::optional<IndexBufferInfo> indexInfo{};
        AttributeBufferInfo positionInfo;
        std::optional<AttributeBufferInfo> normalInfo;
        std::optional<AttributeBufferInfo> tangentInfo;
        std::optional<IndexedAttributeBufferInfos> texcoordInfos;
        glm::dvec3 min;
        glm::dvec3 max;
    };
}