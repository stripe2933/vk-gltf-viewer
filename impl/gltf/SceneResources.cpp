module;

#include <CGAL/Simple_cartesian.h>
#include <CGAL/Min_sphere_of_points_d_traits_3.h>
#include <CGAL/Min_sphere_of_spheres_d.h>
#include <fastgltf/core.hpp>

module vk_gltf_viewer;
import :gltf.SceneResources;

import std;
import :helpers.fastgltf;
import :helpers.ranges;
import :helpers.extended_arithmetic;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [](auto &&x) { return __VA_ARGS__(FWD(x)); }

vk_gltf_viewer::gltf::SceneResources::SceneResources(
    const fastgltf::Asset &asset [[clang::lifetimebound]],
    const AssetResources &assetResources,
    const fastgltf::Scene &scene,
    const vulkan::Gpu &gpu
) : asset { asset },
    gpu { gpu },
    assetResources { assetResources },
    scene { scene } { }

auto vk_gltf_viewer::gltf::SceneResources::getEnclosingSphere() const -> std::pair<glm::dvec3, double> {
    // See https://doc.cgal.org/latest/Bounding_volumes/index.html for the original code.
    using Traits = CGAL::Min_sphere_of_points_d_traits_3<CGAL::Simple_cartesian<double>, double>;
    std::vector<Traits::Point> meshBoundingBoxPoints;

    const auto traverseMeshPrimitivesRecursive = [&](this const auto &self, std::size_t nodeIndex) -> void {
        const fastgltf::Node &node = asset.nodes[nodeIndex];

        if (node.meshIndex) {
            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            const glm::dmat4 transform = nodeWorldTransformBuffer.asRange<const glm::mat4>()[nodeIndex];
            for (const fastgltf::Primitive &primitive : mesh.primitives) {
                const fastgltf::Accessor &accessor = asset.accessors.at(primitive.findAttribute("POSITION")->second);
                const std::span min { std::get_if<std::pmr::vector<double>>(&accessor.min)->data(), 3 };
                const std::span max { std::get_if<std::pmr::vector<double>>(&accessor.max)->data(), 3 };

                for (int i = 0; i < 8; ++i) {
                    const glm::dvec3 boundingBoxPoint { i & 0b100 ? max[0] : min[0], i & 0b010 ? max[1] : min[1], i & 0b001 ? max[2] : min[2] };
                    const glm::dvec3 transformedPoint = toEuclideanCoord(transform * glm::dvec4 { boundingBoxPoint, 1.0 });
                    meshBoundingBoxPoints.emplace_back(transformedPoint.x, transformedPoint.y, transformedPoint.z);
                }
            }
        }

        for (std::size_t childNodeIndex : node.children) {
            self(childNodeIndex);
        }
    };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        traverseMeshPrimitivesRecursive(nodeIndex);
    }

    CGAL::Min_sphere_of_spheres_d<Traits> ms { meshBoundingBoxPoints.begin(), meshBoundingBoxPoints.end() };

    glm::dvec3 center;
    std::copy(ms.center_cartesian_begin(), ms.center_cartesian_end(), value_ptr(center));
    return { center, ms.radius() };
}

auto vk_gltf_viewer::gltf::SceneResources::createOrderedNodePrimitiveInfoPtrs() const -> std::vector<std::pair<std::size_t, const AssetResources::PrimitiveInfo*>> {
    std::vector<std::pair<std::size_t /* nodeIndex */, const AssetResources::PrimitiveInfo*>> nodePrimitiveInfoPtrs;

    // Traverse the scene nodes and collect the glTF mesh primitives with their node indices.
    const auto traverseMeshPrimitivesRecursive
        = [&](this auto self, std::size_t nodeIndex) -> void {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            if (node.meshIndex) {
                const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    const AssetResources::PrimitiveInfo &primitiveInfo = assetResources.primitiveInfos.at(&primitive);
                    nodePrimitiveInfoPtrs.emplace_back(nodeIndex, &primitiveInfo);
                }
            }

            for (std::size_t childNodeIndex : node.children) {
                self(childNodeIndex);
            }
        };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        traverseMeshPrimitivesRecursive(nodeIndex);
    }

    return nodePrimitiveInfoPtrs;
}

auto vk_gltf_viewer::gltf::SceneResources::createNodeWorldTransformBuffer() const -> vku::MappedBuffer {
    std::vector<glm::mat4> nodeTransforms(asset.nodes.size());

    // Traverse the scene nodes and calculate the world transform of each node (by multiplying their local transform to
    // their parent's world transform).
    const auto calculateNodeWorldTransformsRecursive
        = [&](this const auto &self, std::size_t nodeIndex, glm::mat4 parentNodeWorldTransform = { 1.f }) -> void {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            parentNodeWorldTransform *= visit(LIFT(fastgltf::toMatrix), node.transform);
            nodeTransforms[nodeIndex] = parentNodeWorldTransform;

            for (std::size_t childNodeIndex : node.children) {
                self(childNodeIndex, parentNodeWorldTransform);
            }
        };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        calculateNodeWorldTransformsRecursive(nodeIndex);
    }

    return { gpu.allocator, std::from_range, nodeTransforms, vk::BufferUsageFlagBits::eStorageBuffer, vku::allocation::hostRead };
}

auto vk_gltf_viewer::gltf::SceneResources::createPrimitiveBuffer() const -> vku::AllocatedBuffer {
    return vku::MappedBuffer {
        gpu.allocator,
        std::from_range, orderedNodePrimitiveInfoPtrs
            | ranges::views::decompose_transform([](std::size_t nodeIndex, const AssetResources::PrimitiveInfo *pPrimitiveInfo) {
                // If normal and tangent not presented (nullopt), it will use a faceted mesh renderer, and they will does not
                // dereference those buffers. Therefore, it is okay to pass nullptr into shaders
                const auto normalInfo = pPrimitiveInfo->normalInfo.value_or(AssetResources::PrimitiveInfo::AttributeBufferInfo{});
                const auto tangentInfo = pPrimitiveInfo->tangentInfo.value_or(AssetResources::PrimitiveInfo::AttributeBufferInfo{});

                return GpuPrimitive {
                    .pPositionBuffer = pPrimitiveInfo->positionInfo.address,
                    .pNormalBuffer = normalInfo.address,
                    .pTangentBuffer = tangentInfo.address,
                    .pTexcoordAttributeMappingInfoBuffer
                        = ranges::value_or(
                            pPrimitiveInfo->indexedAttributeMappingInfos,
                            AssetResources::IndexedAttribute::Texcoord, {}).pMappingBuffer,
                    .pColorAttributeMappingInfoBuffer
                        = ranges::value_or(
                            pPrimitiveInfo->indexedAttributeMappingInfos,
                            AssetResources::IndexedAttribute::Color, {}).pMappingBuffer,
                    .positionByteStride = pPrimitiveInfo->positionInfo.byteStride,
                    .normalByteStride = normalInfo.byteStride,
                    .tangentByteStride = tangentInfo.byteStride,
                    .nodeIndex = static_cast<std::uint32_t>(nodeIndex),
                    .materialIndex
                        = pPrimitiveInfo->materialIndex.transform([](std::size_t index) {
                            return static_cast<std::int32_t>(index);
                        })
                        .value_or(-1 /* will use the fallback material */),
                };
            }),
        vk::BufferUsageFlagBits::eStorageBuffer,
    }.unmap();
}