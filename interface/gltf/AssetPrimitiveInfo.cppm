module;

#include <cstddef>

export module vk_gltf_viewer:gltf.AssetPrimitiveInfo;

import std;
export import fastgltf;
export import vulkan_hpp;

namespace vk_gltf_viewer::gltf {
    export struct AssetPrimitiveInfo {
        struct IndexBufferInfo { vk::DeviceSize offset; vk::IndexType type; };

        struct AttributeBufferInfo {
            vk::DeviceAddress address;
            std::uint8_t componentType;
            std::uint8_t componentCount;
            std::uint8_t byteStride;
            char _padding_[5];
        };
        static_assert(sizeof(AttributeBufferInfo) == 16);
        static_assert(offsetof(AttributeBufferInfo, componentType) == 8);
        static_assert(offsetof(AttributeBufferInfo, componentCount) == 9);
        static_assert(offsetof(AttributeBufferInfo, byteStride) == 10);

        struct IndexedAttributeBufferInfos { vk::DeviceAddress pMappingBuffer; std::vector<AttributeBufferInfo> attributeInfos; };

        std::uint16_t index;
        std::optional<IndexBufferInfo> indexInfo{};
        AttributeBufferInfo positionInfo;
        std::optional<AttributeBufferInfo> normalInfo;
        std::optional<AttributeBufferInfo> tangentInfo;
        IndexedAttributeBufferInfos texcoordsInfo;
        std::optional<AttributeBufferInfo> colorInfo;
    };
}