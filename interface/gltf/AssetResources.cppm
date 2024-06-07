module;

#include <unordered_map>
#include <vector>

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:gltf.AssetResources;

export import glm;

namespace vk_gltf_viewer::gltf {
    export class AssetResources {
        fastgltf::Expected<fastgltf::GltfDataBuffer> dataBufferExpected;
        fastgltf::Expected<fastgltf::Asset> assetExpected;

    public:
        struct Vertex {
            glm::vec3 position;
            glm::vec3 normal;
        };

        fastgltf::GltfDataBuffer &dataBuffer = dataBufferExpected.get();
        fastgltf::Asset &asset = assetExpected.get();
        std::unordered_map<const fastgltf::Primitive*, std::pair<std::vector<std::uint32_t>, std::vector<Vertex>>> primitiveData;

        AssetResources(const std::filesystem::path &path, fastgltf::Parser &parser);

    private:
        [[nodiscard]] auto loadDataBuffer(const std::filesystem::path &path) const -> decltype(dataBufferExpected);
        [[nodiscard]] auto loadAsset(fastgltf::Parser &parser, const std::filesystem::path &parentPath) -> decltype(assetExpected);
    };
}