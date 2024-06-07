module;

#include <compare>
#include <unordered_map>
#include <vector>

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:gltf.AssetResources;

export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    export class AssetResources {
        fastgltf::Expected<fastgltf::GltfDataBuffer> dataBufferExpected;
        fastgltf::GltfDataBuffer &dataBuffer = dataBufferExpected.get();
        fastgltf::Expected<fastgltf::Asset> assetExpected;

    public:
        struct PrimitiveData {
            struct IndexBufferInfo { vk::Buffer buffer; vk::DeviceSize offset; vk::IndexType type; std::uint32_t drawCount; };
            struct VertexBufferInfo { vk::DeviceAddress address; vk::DeviceSize byteStride; };

            IndexBufferInfo indexInfo;
            VertexBufferInfo positionInfo, normalInfo;
        };

        fastgltf::Asset &asset = assetExpected.get();

        std::unordered_map<std::size_t, vku::MappedBuffer> buffers;
        std::unordered_map<const fastgltf::Primitive*, PrimitiveData> primitiveData;

        AssetResources(const std::filesystem::path &path, fastgltf::Parser &parser, const vulkan::Gpu &gpu);

    private:
        [[nodiscard]] auto loadDataBuffer(const std::filesystem::path &path) const -> decltype(dataBufferExpected);
        [[nodiscard]] auto loadAsset(fastgltf::Parser &parser, const std::filesystem::path &parentPath) -> decltype(assetExpected);
        [[nodiscard]] auto createBuffers(const vulkan::Gpu &gpu) const -> decltype(buffers);
    };
}