module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:gltf.AssetExternalBuffers;

import std;

namespace vk_gltf_viewer::gltf {
    /**
     * A storage for <tt>fastgltf::Asset</tt>'s external buffers. This loads the external and GLB buffers at construction,
     * and organize them into <tt>std::span<const std::byte></tt> by their indices. Since this operation done in the
     * initialization, you don't have to make branches for <tt>fastgltf::DataSource</tt> variant type.
     *
     * Also, this class implements <tt>operator(const fastgltf::Buffer&) const -> const std::byte*</tt> for compatibility
     * with <tt>fastgltf::DefaultBufferDataAdapter</tt>. You can directly pass the class instance as the fastgltf's
     * buffer data adaptor, such like <tt>fastgltf::iterateAccessor</tt>.
     */
    export class AssetExternalBuffers {
        const fastgltf::Buffer *pFirstBuffer;
        std::vector<std::vector<std::byte>> cache;

    public:
        std::vector<std::span<const std::byte>> bytes;

        AssetExternalBuffers(const fastgltf::Asset &asset, const std::filesystem::path &assetDir)
            : pFirstBuffer { asset.buffers.data() }
            , bytes { createBufferBytes(asset, assetDir) } { }

        /**
         * Interface for <tt>fastgltf::BufferDataAdapter</tt>.
         * @param buffer buffer to get the byte address.
         * @return First byte address of the buffer.
         */
        [[nodiscard]] auto operator()(const fastgltf::Buffer &buffer) const -> const std::byte* {
            const std::size_t bufferIndex = &buffer - pFirstBuffer;
            return bytes[bufferIndex].data();
        }

        [[nodiscard]] auto getByteRegion(const fastgltf::BufferView &bufferView) const noexcept -> std::span<const std::byte> {
            return bytes[bufferView.bufferIndex].subspan(bufferView.byteOffset, bufferView.byteLength);
        }

    private:
        [[nodiscard]] auto createBufferBytes(const fastgltf::Asset &asset, const std::filesystem::path &assetDir) -> std::vector<std::span<const std::byte>> {
            return asset.buffers
                | std::views::transform([&](const fastgltf::Buffer &buffer) {
                    return visit(fastgltf::visitor {
                        [](const fastgltf::sources::Array &array) -> std::span<const std::byte> {
                            return as_bytes(std::span { array.bytes });
                        },
                        [](const fastgltf::sources::ByteView &byteView) -> std::span<const std::byte> {
                            return byteView.bytes;
                        },
                        [&](const fastgltf::sources::URI &uri) -> std::span<const std::byte> {
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
                        [](const auto&) -> std::span<const std::byte> {
                            throw std::runtime_error { "Unsupported source data type" };
                        },
                    }, buffer.data);
                })
                | std::ranges::to<std::vector>();
        }
    };
}