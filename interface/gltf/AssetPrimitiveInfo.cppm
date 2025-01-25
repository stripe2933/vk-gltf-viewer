module;

#include <cstddef>

export module vk_gltf_viewer:gltf.AssetPrimitiveInfo;

import std;
export import fastgltf;
export import glm;
export import vulkan_hpp;

namespace vk_gltf_viewer::gltf {
    struct AssetPrimitiveInfo {
        struct IndexBufferInfo {
            vk::DeviceSize offset;
            vk::IndexType type;
        };

        struct AttributeBufferInfo {
            vk::DeviceAddress address;
            std::uint8_t byteStride;
            std::uint8_t componentType;
        };
        static_assert(sizeof(AttributeBufferInfo) == 16);
        static_assert(offsetof(AttributeBufferInfo, address) == 0);
        static_assert(offsetof(AttributeBufferInfo, byteStride) == 8);
        static_assert(offsetof(AttributeBufferInfo, componentType) == 9);

        struct ColorAttributeBufferInfo final : AttributeBufferInfo {
            std::uint8_t componentCount;
        };

        struct IndexedAttributeBufferInfos {
            vk::DeviceAddress pMappingBuffer;
            std::vector<AttributeBufferInfo> attributeInfos;
        };

        std::uint16_t index;
        std::optional<std::size_t> materialIndex;
        std::uint32_t drawCount;
        std::optional<IndexBufferInfo> indexInfo{};
        AttributeBufferInfo positionInfo;
        IndexedAttributeBufferInfos positionMorphTargetInfos;
        std::optional<AttributeBufferInfo> normalInfo;
        IndexedAttributeBufferInfos normalMorphTargetInfos;
        std::optional<AttributeBufferInfo> tangentInfo;
        IndexedAttributeBufferInfos tangentMorphTargetInfos;
        IndexedAttributeBufferInfos texcoordsInfo;
        std::optional<ColorAttributeBufferInfo> colorInfo;
        glm::dvec3 min;
        glm::dvec3 max;
    };
}