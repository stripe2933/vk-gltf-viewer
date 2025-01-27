export module vk_gltf_viewer:gltf.AssetPrimitiveInfo;

import std;
export import fastgltf;
export import vulkan_hpp;

namespace vk_gltf_viewer::gltf {
    export struct AssetPrimitiveInfo {
        struct IndexBufferInfo { vk::DeviceSize offset; vk::IndexType type; };
        struct AttributeBufferInfo { vk::DeviceAddress address; std::uint8_t byteStride; fastgltf::ComponentType componentType; };
        struct ColorAttributeBufferInfo final : AttributeBufferInfo { std::uint8_t numComponent; };
        struct IndexedAttributeBufferInfos { vk::DeviceAddress pMappingBuffer; std::vector<AttributeBufferInfo> attributeInfos; };

        std::uint16_t index;
        std::optional<IndexBufferInfo> indexInfo{};
        AttributeBufferInfo positionInfo;
        std::optional<AttributeBufferInfo> normalInfo;
        std::optional<AttributeBufferInfo> tangentInfo;
        IndexedAttributeBufferInfos texcoordsInfo;
        std::optional<ColorAttributeBufferInfo> colorInfo;
    };
}