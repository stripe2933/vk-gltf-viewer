module;

#include <CGAL/Cartesian.h>
#include <CGAL/Min_sphere_of_spheres_d.h>
#include <fastgltf/core.hpp>

module vk_gltf_viewer;
import :gltf.SceneResources;

import std;
import :helpers.ranges;

vk_gltf_viewer::gltf::SceneResources::SceneResources(
    const fastgltf::Asset &asset [[clang::lifetimebound]],
    const AssetResources &assetResources,
    const fastgltf::Scene &scene,
    const vulkan::Gpu &gpu
) : asset { asset },
    gpu { gpu },
    assetResources { assetResources },
    scene { scene } { }

auto vk_gltf_viewer::gltf::SceneResources::getSmallestEnclosingSphere() const -> std::pair<glm::dvec3, double> {
    using Traits = CGAL::Min_sphere_of_spheres_d_traits_3<CGAL::Cartesian<double>, double>;
    std::vector<Traits::Sphere> meshBoundingSpheres;

    const auto traverseMeshPrimitivesRecursive
        = [&](this const auto &self, std::size_t nodeIndex) -> void {
            const fastgltf::Node &node = asset.nodes[nodeIndex];

            if (node.meshIndex) {
                const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                const glm::dmat4 transform = nodeWorldTransformBuffer.asRange<const glm::mat4>()[nodeIndex];
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    const fastgltf::Accessor &accessor = asset.accessors.at(primitive.findAttribute("POSITION")->second);
                    const glm::dvec3 min = glm::make_vec3<double>(std::get_if<std::pmr::vector<double>>(&accessor.min)->data());
                    const glm::dvec3 max = glm::make_vec3<double>(std::get_if<std::pmr::vector<double>>(&accessor.max)->data());

                    const glm::dvec3 transformedMin = transform * glm::dvec4 { min, 1.0 };
                    const glm::dvec3 transformedMax = transform * glm::dvec4 { max, 1.0 };

                    const glm::dvec3 halfDisplacement = (transformedMax - transformedMin) / 2.0;
                    const glm::dvec3 center = transformedMin + halfDisplacement;
                    const double radius = length(halfDisplacement);
                    meshBoundingSpheres.emplace_back(Traits::Point { center.x, center.y, center.z }, radius);
                }
            }

            for (std::size_t childNodeIndex : node.children) {
                self(childNodeIndex);
            }
    };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        traverseMeshPrimitivesRecursive(nodeIndex);
    }

    using Min_sphere = CGAL::Min_sphere_of_spheres_d<Traits>;
    Min_sphere ms { meshBoundingSpheres.begin(), meshBoundingSpheres.end() };

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
            parentNodeWorldTransform *= visit(fastgltf::visitor {
                [](const fastgltf::TRS &trs) {
                    return translate(glm::mat4 { 1.f }, glm::make_vec3(trs.translation.data()))
                        * mat4_cast(glm::make_quat(trs.rotation.data()))
                        * scale(glm::mat4 { 1.f }, glm::make_vec3(trs.scale.data()));
                },
                [](const fastgltf::Node::TransformMatrix &mat) {
                    return glm::make_mat4(mat.data());
                },
            }, node.transform);
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