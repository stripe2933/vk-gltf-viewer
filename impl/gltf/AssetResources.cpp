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
import :helpers.type_map;

vk_gltf_viewer::gltf::AssetResources::AssetResources(
    const std::filesystem::path &path,
    fastgltf::Parser &parser
) : dataBufferExpected { loadDataBuffer(path) },
    assetExpected { loadAsset(parser, path.parent_path()) } {
    for (const fastgltf::Primitive &primitive : asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join) {
        assert(primitive.type == fastgltf::PrimitiveType::Triangles && "Non-triangle primitives are not supported");
        assert(primitive.indicesAccessor && "Missing indices accessor");
        assert(primitive.findAttribute("NORMAL") != primitive.attributes.end() && "Missing normal attribute");

        const fastgltf::Accessor &indicesAccessor = asset.accessors[*primitive.indicesAccessor];
        std::vector<std::uint32_t> indices;
        indices.reserve(indicesAccessor.count);

        constexpr type_map mapping {
            make_type_map_entry<std::uint16_t>(fastgltf::ComponentType::UnsignedShort),
            make_type_map_entry<std::uint32_t>(fastgltf::ComponentType::UnsignedInt),
        };
        visit(fastgltf::visitor {
            [&]<std::unsigned_integral IndexType>(std::type_identity<IndexType>) {
                for (std::uint32_t index : iterateAccessor<IndexType>(asset, indicesAccessor)) {
                    indices.push_back(index);
                }
            },
        }, mapping.get_variant(indicesAccessor.componentType));

        std::vector<Vertex> vertices(asset.accessors[primitive.findAttribute("POSITION")->second].count);
        iterateAccessorWithIndex<glm::vec3>(asset, asset.accessors[primitive.findAttribute("POSITION")->second], [&](const glm::vec3 &position, auto idx) {
            vertices[idx].position = position;
        });
        iterateAccessorWithIndex<glm::vec3>(asset, asset.accessors[primitive.findAttribute("NORMAL")->second], [&](const glm::vec3 &normal, auto idx) {
            vertices[idx].normal = normal;
        });

        primitiveData.try_emplace(&primitive, std::move(indices), std::move(vertices));
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
    auto asset = parser.loadGltf(dataBufferExpected.get(), parentPath, fastgltf::Options::LoadExternalBuffers);
    if (auto error = asset.error(); error != fastgltf::Error::None) {
        throw std::runtime_error { std::format("Failed to load glTF asset: {}", getErrorMessage(error)) };
    }

    return asset;
}