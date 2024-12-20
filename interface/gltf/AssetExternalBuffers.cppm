module;

#include <cerrno>
#include <cstring>

export module vk_gltf_viewer:gltf.AssetExternalBuffers;

import std;
export import fastgltf;
export import :gltf.AssetProcessError;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief A storage for <tt>fastgltf::Asset</tt>'s external buffers, and also an adapter of fastgltf's <tt>DataBufferAdapter</tt>.
     *
     * This loads the external and GLB buffers at construction, and organize them into <tt>std::span<const std::byte></tt> by their indices. Since this operation done in the initialization, you don't have to make branches for <tt>fastgltf::DataSource</tt> variant type.
     *
     * Also, this class implements <tt>const std::byte* operator(const fastgltf::Asset&, std::size_t) const</tt> for compatibility with <tt>fastgltf::DefaultBufferDataAdapter</tt>. You can directly pass the class instance as the fastgltf's buffer data adapter, such like <tt>fastgltf::iterateAccessor</tt>.
     */
    export class AssetExternalBuffers {
        std::vector<std::unique_ptr<std::byte[]>> cache;
        std::vector<std::span<const std::byte>> bytes;

    public:
        AssetExternalBuffers(const fastgltf::Asset &asset, const std::filesystem::path &directory)
            : bytes { createBufferBytes(asset, directory) } { }

        /**
         * Interface for <tt>fastgltf::BufferDataAdapter</tt>.
         * @param asset asset that contains the buffer.
         * @param bufferViewIndex Index of the buffer view.
         * @return First byte address of the buffer.
         */
        [[nodiscard]] std::span<const std::byte> operator()(const fastgltf::Asset &asset, std::size_t bufferViewIndex) const {
            const fastgltf::BufferView &bufferView = asset.bufferViews[bufferViewIndex];
            return bytes[bufferView.bufferIndex].subspan(bufferView.byteOffset, bufferView.byteLength);
        }

    private:
        [[nodiscard]] std::vector<std::span<const std::byte>> createBufferBytes(
            const fastgltf::Asset &asset,
            const std::filesystem::path &directory
        ) {
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
                            if (!uri.uri.isLocalPath()) throw AssetProcessError::UnsupportedSourceDataType;

                            std::ifstream file { directory / uri.uri.fspath(), std::ios::binary };
                            if (!file) throw std::runtime_error { std::format("Failed to open file: {} (error code={})", strerror(errno), errno) };

                            // Determine file size.
                            file.seekg(0, std::ios::end);
                            const std::size_t fileSize = file.tellg();

                            // Note: calling std::vector::emplace_back may invalidate the elements' references, but each
                            // std::unique_ptr is remained. Therefore, it is safe to use.
                            const std::size_t dataSize = fileSize - uri.fileByteOffset;
                            auto &data = cache.emplace_back(std::make_unique<std::byte[]>(dataSize));
                            file.seekg(uri.fileByteOffset);
                            file.read(reinterpret_cast<char*>(data.get()), dataSize);

                            return { data.get(), dataSize };
                        },
                        // Note: fastgltf::source::{BufferView,Vector} should not be handled since they are not used
                        // for fastgltf::Buffer::data.
                        [](const auto&) -> std::span<const std::byte> {
                            throw AssetProcessError::UnsupportedSourceDataType;
                        },
                    }, buffer.data);
                })
                | std::ranges::to<std::vector>();
        }
    };
}