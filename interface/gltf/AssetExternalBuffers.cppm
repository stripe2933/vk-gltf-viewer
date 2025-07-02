module;

#include <meshoptimizer.h>

export module vk_gltf_viewer.gltf.AssetExternalBuffers;

import std;
export import fastgltf;

export import vk_gltf_viewer.gltf.AssetProcessError;
import vk_gltf_viewer.helpers.io;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief A storage for <tt>fastgltf::Asset</tt>'s external buffers, and also an adapter of fastgltf's <tt>DataBufferAdapter</tt>.
     *
     * This loads the external and GLB buffers at construction, and organize them into <tt>std::span<const std::byte></tt> by their indices. Since this operation done in the initialization, you don't have to make branches for <tt>fastgltf::DataSource</tt> variant type.
     *
     * Also, this class implements <tt>const std::byte* operator(const fastgltf::Asset&, std::size_t) const</tt> for compatibility with <tt>fastgltf::DefaultBufferDataAdapter</tt>. You can directly pass the class instance as the fastgltf's buffer data adapter, such like <tt>fastgltf::iterateAccessor</tt>.
     */
    export class AssetExternalBuffers {
        std::unordered_map<std::size_t, std::vector<std::byte>> externalBufferBytes;
        std::vector<std::unique_ptr<std::byte[]>> meshoptDecompressedBytes;
        std::vector<std::span<const std::byte>> bufferViewBytes;

    public:
        AssetExternalBuffers(const fastgltf::Asset &asset, const std::filesystem::path &directory);

        /**
         * Interface for <tt>fastgltf::BufferDataAdapter</tt>.
         * @param asset asset that contains the buffer.
         * @param bufferViewIndex Index of the buffer view.
         * @return First byte address of the buffer.
         */
        [[nodiscard]] std::span<const std::byte> operator()(const fastgltf::Asset &asset, std::size_t bufferViewIndex) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::gltf::AssetExternalBuffers::AssetExternalBuffers(const fastgltf::Asset &asset, const std::filesystem::path &directory) {
    const auto getBufferBytes = [&](std::size_t bufferIndex) -> std::span<const std::byte> {
        return visit(fastgltf::visitor {
            [](const fastgltf::sources::Array &array) -> std::span<const std::byte> {
                return as_bytes(std::span { array.bytes });
            },
            [](const fastgltf::sources::ByteView &byteView) -> std::span<const std::byte> {
                return byteView.bytes;
            },
            [&](const fastgltf::sources::URI &uri) -> std::span<const std::byte> {
                auto it = externalBufferBytes.find(bufferIndex);
                if (it == externalBufferBytes.end()) {
                    if (!uri.uri.isLocalPath()) throw AssetProcessError::UnsupportedSourceDataType;
                    it = externalBufferBytes.emplace_hint(it, bufferIndex, loadFileAsBinary(directory / uri.uri.fspath()));
                }

                return it->second;
            },
            // Note: fastgltf::source::{BufferView,Vector} should not be handled since they are not used
            // for fastgltf::Buffer::data.
            [](const auto&) -> std::span<const std::byte> {
                throw AssetProcessError::UnsupportedSourceDataType;
            },
        }, asset.buffers[bufferIndex].data);
    };

    bufferViewBytes.reserve(asset.bufferViews.size());
    for (const fastgltf::BufferView &bufferView : asset.bufferViews) {
        if (const auto &mc = bufferView.meshoptCompression) {
            const auto compressed = reinterpret_cast<const unsigned char*>(getBufferBytes(mc->bufferIndex).data()) + mc->byteOffset;

            const std::size_t decompressedBufferSize = mc->count * mc->byteStride;
            std::byte* const decompressed = meshoptDecompressedBytes.emplace_back(std::make_unique_for_overwrite<std::byte[]>(decompressedBufferSize)).get();

            int rc = -1;
            switch (mc->mode) {
                case fastgltf::MeshoptCompressionMode::Attributes:
                    rc = meshopt_decodeVertexBuffer(decompressed, mc->count, mc->byteStride, compressed, mc->byteLength);
                    break;
                case fastgltf::MeshoptCompressionMode::Triangles:
                    rc = meshopt_decodeIndexBuffer(decompressed, mc->count, mc->byteStride, compressed, mc->byteLength);
                    break;
                case fastgltf::MeshoptCompressionMode::Indices:
                    rc = meshopt_decodeIndexSequence(decompressed, mc->count, mc->byteStride, compressed, mc->byteLength);
                    break;
            }

            if (rc != 0) {
                throw std::runtime_error { "Failed to decompress EXT_meshopt_compression compressed buffer view." };
            }

            switch (mc->filter) {
                case fastgltf::MeshoptCompressionFilter::None:
                    break;
                case fastgltf::MeshoptCompressionFilter::Octahedral:
                    meshopt_decodeFilterOct(decompressed, mc->count, mc->byteStride);
                    break;
                case fastgltf::MeshoptCompressionFilter::Quaternion:
                    meshopt_decodeFilterQuat(decompressed, mc->count, mc->byteStride);
                    break;
                case fastgltf::MeshoptCompressionFilter::Exponential:
                    meshopt_decodeFilterExp(decompressed, mc->count, mc->byteStride);
                    break;
            }

            bufferViewBytes.emplace_back(decompressed, decompressedBufferSize);
        }
        else {
            const std::span<const std::byte> bufferBytes = getBufferBytes(bufferView.bufferIndex);
            bufferViewBytes.push_back( bufferBytes.subspan(bufferView.byteOffset, bufferView.byteLength));
        }
    }
}

std::span<const std::byte> vk_gltf_viewer::gltf::AssetExternalBuffers::operator()(const fastgltf::Asset &asset, std::size_t bufferViewIndex) const {
    return bufferViewBytes[bufferViewIndex];
}