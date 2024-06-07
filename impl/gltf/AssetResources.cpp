module;

#include <format>
#include <ranges>
#include <stdexcept>
#include <vector>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

module vk_gltf_viewer;
import :gltf.AssetResources;

vk_gltf_viewer::gltf::AssetResources::AssetResources(
    const std::filesystem::path &path,
    fastgltf::Parser &parser,
    const vulkan::Gpu &gpu
) : dataBufferExpected { loadDataBuffer(path) },
    assetExpected { loadAsset(parser, path.parent_path()) },
    buffers { createBuffers(gpu) } {
    for (const fastgltf::Primitive &primitive : asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join) {
        assert(primitive.type == fastgltf::PrimitiveType::Triangles && "Non-triangle primitives are not supported");
        assert(primitive.findAttribute("NORMAL") != primitive.attributes.end() && "Missing normal attribute");

        const fastgltf::Accessor &indicesAccessor = asset.accessors[*primitive.indicesAccessor],
                                 &positionAccessor = asset.accessors[primitive.findAttribute("POSITION")->second],
                                 &normalAccessor = asset.accessors[primitive.findAttribute("NORMAL")->second];
        primitiveData.try_emplace(
            &primitive,
            PrimitiveData {
                .indexInfo = {
                    .buffer = buffers.at(*indicesAccessor.bufferViewIndex),
                    .offset = indicesAccessor.byteOffset,
                    .type = [&]() {
                        switch (indicesAccessor.componentType) {
                            case fastgltf::ComponentType::UnsignedShort: return vk::IndexType::eUint16;
                            case fastgltf::ComponentType::UnsignedInt: return vk::IndexType::eUint32;
                            default: throw std::runtime_error { "Unsupported index component type" };
                        }
                    }(),
                    .drawCount = static_cast<std::uint32_t>(indicesAccessor.count),
                },
                .positionInfo = {
                    .address = gpu.device.getBufferAddress({ buffers.at(*positionAccessor.bufferViewIndex) }) + positionAccessor.byteOffset,
                    .byteStride = asset.bufferViews[*positionAccessor.bufferViewIndex].byteStride.value_or(getElementByteSize(positionAccessor.type, positionAccessor.componentType)),
                },
                .normalInfo = {
                    .address = gpu.device.getBufferAddress({ buffers.at(*normalAccessor.bufferViewIndex) }) + normalAccessor.byteOffset,
                    .byteStride = asset.bufferViews[*normalAccessor.bufferViewIndex].byteStride.value_or(getElementByteSize(normalAccessor.type, normalAccessor.componentType)),
                },
            });
    }
}

auto vk_gltf_viewer::gltf::AssetResources::loadDataBuffer(
    const std::filesystem::path &path
) const -> decltype(dataBufferExpected) {
    auto dataBuffer = fastgltf::GltfDataBuffer::FromPath(path);
    if (auto error = dataBuffer.error(); error != fastgltf::Error::None) {
        throw std::runtime_error { std::format("Failed to load glTF data buffer: {}", getErrorMessage(error)) };
    }

    return dataBuffer;
}

auto vk_gltf_viewer::gltf::AssetResources::loadAsset(
    fastgltf::Parser &parser,
    const std::filesystem::path &parentPath
) -> decltype(assetExpected) {
    auto asset = parser.loadGltf(dataBuffer, parentPath, fastgltf::Options::LoadExternalBuffers);
    if (auto error = asset.error(); error != fastgltf::Error::None) {
        throw std::runtime_error { std::format("Failed to load glTF asset: {}", getErrorMessage(error)) };
    }

    return asset;
}

auto vk_gltf_viewer::gltf::AssetResources::createBuffers(
    const vulkan::Gpu &gpu
) const -> decltype(buffers) {
    const std::vector bufferViewBytes
        = asset.bufferViews
        | std::views::transform([this](const fastgltf::BufferView &bufferView) {
            const std::span bufferBytes = get<fastgltf::sources::Array>(asset.buffers[bufferView.bufferIndex].data).bytes;
            return bufferBytes.subspan(bufferView.byteOffset, bufferView.byteLength);
        })
        | std::ranges::to<std::vector<std::span<std::uint8_t>>>();

    // Get asset buffer views that are storing vertices. Calculate appropriate Vulkan buffer type for them.
    std::unordered_map<std::size_t, vk::BufferUsageFlags> bufferUsages;
    for (const fastgltf::Primitive &primitive : asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join) {
        constexpr auto checkAccessorValidity = [](const fastgltf::Accessor &accessor){
            if (accessor.sparse) throw std::runtime_error { "Sparse accessor not supported" };
            if (!accessor.bufferViewIndex) throw std::runtime_error { "Missing accessor buffer view index" };
        };

        if (!primitive.indicesAccessor) throw std::runtime_error { "Missing indices accessor" };
        const fastgltf::Accessor &indicesAccessor = asset.accessors[*primitive.indicesAccessor];

        // Check index accessor validity.
        checkAccessorValidity(indicesAccessor);
        const bool isIndexInterleaved
            = asset.bufferViews[*indicesAccessor.bufferViewIndex].byteStride
            .transform([&](std::size_t stride) { return stride != getElementByteSize(indicesAccessor.type, indicesAccessor.componentType); })
            .value_or(false);
        if (isIndexInterleaved) throw std::runtime_error { "Interleaved index buffer not supported" };

        bufferUsages[*indicesAccessor.bufferViewIndex] |= vk::BufferUsageFlagBits::eIndexBuffer;

        const auto accessors
            = primitive.attributes
            | std::views::values
            | std::views::transform([&](std::size_t i) -> decltype(auto) { return asset.accessors[i]; });
        for (const fastgltf::Accessor &accessor : accessors) {
            checkAccessorValidity(accessor);
            bufferUsages[*accessor.bufferViewIndex] |= vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        }
    }

    return bufferUsages
        | std::views::transform([&](const auto &keyValue) {
            const auto [bufferViewIndex, bufferUsage] = keyValue;
            return std::pair<std::size_t, vku::MappedBuffer> {
                std::piecewise_construct,
                std::tuple { bufferViewIndex },
                std::tuple { gpu.allocator, std::from_range, bufferViewBytes[bufferViewIndex], bufferUsage },
            };
        })
        | std::ranges::to<std::unordered_map<std::size_t, vku::MappedBuffer>>();
}