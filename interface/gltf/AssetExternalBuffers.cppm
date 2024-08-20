module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:gltf.AssetExternalBuffers;

import std;

namespace vk_gltf_viewer::gltf {
    export class AssetExternalBuffers {
        std::vector<std::vector<std::uint8_t>> cache;

    public:
        std::vector<std::span<const std::uint8_t>> bytes;

        AssetExternalBuffers(const fastgltf::Asset &asset, const std::filesystem::path &assetDir)
            : bytes { createBufferBytes(asset, assetDir) } { }

        [[nodiscard]] auto getByteRegion(const fastgltf::BufferView &bufferView) const noexcept -> std::span<const std::uint8_t> {
            return bytes[bufferView.bufferIndex].subspan(bufferView.byteOffset, bufferView.byteLength);
        }

    private:
        [[nodiscard]] auto createBufferBytes(const fastgltf::Asset &asset, const std::filesystem::path &assetDir) -> std::vector<std::span<const std::uint8_t>> {
            return asset.buffers
                | std::views::transform([&](const fastgltf::Buffer &buffer) {
                    return visit(fastgltf::visitor {
                        [](const fastgltf::sources::Array &array) -> std::span<const std::uint8_t> {
                            return array.bytes;
                        },
                        [](const fastgltf::sources::ByteView &byteView) -> std::span<const std::uint8_t> {
                            return { reinterpret_cast<const std::uint8_t*>(byteView.bytes.data()), byteView.bytes.size() };
                        },
                        [&](const fastgltf::sources::URI &uri) -> std::span<const std::uint8_t> {
                            if (!uri.uri.isLocalPath()) throw std::runtime_error { "Non-local source URI not supported." };

                            std::ifstream file { assetDir / uri.uri.fspath(), std::ios::binary };
                            if (!file) throw std::runtime_error { std::format("Failed to open file: {} (error code={})", strerror(errno), errno) };

                            // Determine file size.
                            file.seekg(0, std::ios::end);
                            const std::size_t fileSize = file.tellg();

                            // Note: calling std::vector::emplace_back may invalidate the elements' references, but the
                            // element vector's data pointer will be remained. Therefore, it is safe to use std::vector
                            // cache.
                            auto &data = cache.emplace_back(fileSize - uri.fileByteOffset);
                            file.seekg(uri.fileByteOffset);
                            file.read(reinterpret_cast<char*>(data.data()), data.size());

                            return data;
                        },
                        [](const auto&) -> std::span<const std::uint8_t> {
                            throw std::runtime_error { "Unsupported source data type" };
                        },
                    }, buffer.data);
                })
                | std::ranges::to<std::vector>();
        }
    };
}