module;

#include <CGAL/Simple_cartesian.h>
#include <CGAL/Min_sphere_of_points_d_traits_3.h>
#include <CGAL/Min_sphere_of_spheres_d.h>

export module vk_gltf_viewer:gltf.algorithm.miniball;

import std;
export import fastgltf;
export import glm;
import :gltf.algorithm.bounding_box;
import :gltf.algorithm.traversal;
import :helpers.concepts;
import :helpers.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return (__VA_ARGS__)(FWD(xs)...); }

namespace vk_gltf_viewer::gltf::algorithm {
    /**
     * @brief The smallest enclosing sphere of the scene meshes' bounding boxes, i.e. miniball.
     *
     * @tparam MeshNodeTransformGetter A function type that return world transform matrix for an instance of a node. First parameter is the node index, and the second parameter is the instance index. If node doesn't have instanced mesh, 0 will be passed for the second parameter.
     * @param asset fastgltf Asset.
     * @param scene Scene to be considered. It must be from the same asset.
     * @param transformGetter A function that follows the MeshNodeTransformGetter concept.
     * @return The pair of the miniball's center and radius.
     */
    template <concepts::compatible_signature_of<glm::dmat4, std::size_t, std::size_t> MeshNodeTransformGetter>
    [[nodiscard]] std::pair<glm::dvec3, double> getMiniball(const fastgltf::Asset &asset, const fastgltf::Scene &scene, const MeshNodeTransformGetter &transformGetter) {
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
            const auto collectTransformedBoundingBoxPoints = [&](const glm::dmat4 &worldTransform) {
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    for (const glm::dvec3 &point : getBoundingBoxCornerPoints(asset, primitive)) {
                        const glm::dvec3 transformedPoint { worldTransform * glm::dvec4 { point, 1.0 } };
                        meshBoundingBoxPoints.emplace_back(transformedPoint.x, transformedPoint.y, transformedPoint.z);
                    }
                }
            };

            if (node.instancingAttributes.empty()) {
                collectTransformedBoundingBoxPoints(transformGetter(nodeIndex, 0U));
            }
            else {
                for (std::size_t instanceIndex : ranges::views::upto(asset.accessors[node.instancingAttributes[0].accessorIndex].count)) {
                    collectTransformedBoundingBoxPoints(transformGetter(nodeIndex, instanceIndex));
                }
            }
        });

        CGAL::Min_sphere_of_spheres_d<Traits> ms { meshBoundingBoxPoints.begin(), meshBoundingBoxPoints.end() };

        glm::dvec3 center;
        std::copy(ms.center_cartesian_begin(), ms.center_cartesian_end(), value_ptr(center));
        return { center, ms.radius() };
    }
}