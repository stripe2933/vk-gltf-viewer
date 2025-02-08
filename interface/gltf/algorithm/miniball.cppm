module;

#include <CGAL/Simple_cartesian.h>
#include <CGAL/Min_sphere_of_points_d_traits_3.h>
#include <CGAL/Min_sphere_of_spheres_d.h>

export module vk_gltf_viewer:gltf.algorithm.miniball;

import std;
export import fastgltf;
import :helpers.fastgltf;
import :gltf.algorithm.bounding_box;
import :gltf.algorithm.traversal;
export import :gltf.NodeWorldTransforms;

namespace vk_gltf_viewer::gltf::algorithm {
    /**
     * @brief The smallest enclosing sphere of the scene meshes' bounding boxes, i.e. miniball.
     *
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
     * @param asset fastgltf Asset.
     * @param scene Scene to be considered. It must be from the same asset.
     * @param nodeWorldTransforms Pre-calculated world transforms for each node in the scene.
     * @return The pair of the miniball's center and radius.
     */
    template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    [[nodiscard]] std::pair<fastgltf::math::dvec3, double> getMiniball(
        const fastgltf::Asset &asset,
        const fastgltf::Scene &scene,
        const NodeWorldTransforms &nodeWorldTransforms,
        const BufferDataAdapter &adapter = {}
    ) {
        // See https://doc.cgal.org/latest/Bounding_volumes/index.html for the original code.
        using Traits = CGAL::Min_sphere_of_points_d_traits_3<CGAL::Simple_cartesian<double>, double>;
        std::vector<Traits::Point> meshBoundingBoxPoints;

        traverseScene(asset, scene, [&](std::size_t nodeIndex) {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            if (!node.meshIndex) {
                // Node without mesh is not considered for miniball calculation.
                return;
            }

            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            const auto collectTransformedBoundingBoxPoints = [&](const fastgltf::math::dmat4x4 &worldTransform) {
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    for (const fastgltf::math::dvec3 &point : getBoundingBoxCornerPoints(primitive, node, asset)) {
                        const fastgltf::math::dvec3 transformedPoint { worldTransform * fastgltf::math::dvec4 { point.x(), point.y(), point.z(), 1.0 } };
                        meshBoundingBoxPoints.emplace_back(transformedPoint.x(), transformedPoint.y(), transformedPoint.z());
                    }
                }
            };

            const fastgltf::math::fmat4x4 &worldTransform = nodeWorldTransforms[nodeIndex];
            if (node.instancingAttributes.empty()) {
                collectTransformedBoundingBoxPoints(cast<double>(worldTransform));
            }
            else {
                for (const fastgltf::math::fmat4x4 &instanceTransform : getInstanceTransforms(asset, nodeIndex, adapter)) {
                    collectTransformedBoundingBoxPoints(cast<double>(worldTransform * instanceTransform));
                }
            }
        });

        CGAL::Min_sphere_of_spheres_d<Traits> ms { meshBoundingBoxPoints.begin(), meshBoundingBoxPoints.end() };

        fastgltf::math::dvec3 center;
        std::copy(ms.center_cartesian_begin(), ms.center_cartesian_end(), center.data());
        return { center, ms.radius() };
    }
}