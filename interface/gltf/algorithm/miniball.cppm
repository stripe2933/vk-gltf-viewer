module;

#include <CGAL/Simple_cartesian.h>
#include <CGAL/Min_sphere_of_points_d_traits_3.h>
#include <CGAL/Min_sphere_of_spheres_d.h>
#include <fastgltf/types.hpp>

export module vk_gltf_viewer:gltf.algorithm.miniball;

import std;
export import glm;
import :helpers.fastgltf;
import :math.extended_arithmetic;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [](auto &&x) { return __VA_ARGS__(FWD(x)); }

namespace vk_gltf_viewer::gltf::algorithm {
    /**
     * @brief The smallest enclosing sphere of the scene meshes' bounding boxes, i.e. miniball.
     *
     * If you have already calculated the node transforms, use the overload with \p nodeWorldTransforms to avoid redundant
     * matrix multiplication.
     *
     * @param asset fastgltf Asset.
     * @param scene Scene to be considered. It must be from the same asset.
     * @return The pair of the miniball's center and radius.
     */
    [[nodiscard]] std::pair<glm::dvec3, double> getMiniball(const fastgltf::Asset &asset, const fastgltf::Scene &scene) {
        // See https://doc.cgal.org/latest/Bounding_volumes/index.html for the original code.
        using Traits = CGAL::Min_sphere_of_points_d_traits_3<CGAL::Simple_cartesian<double>, double>;
        std::vector<Traits::Point> meshBoundingBoxPoints;

        // FIXME: Due to the Clang 18 explicit object parameter bug, redundant const fastgltf::Asset& parameter added.
        const auto traverseSceneNodeRecursive = [&](this const auto &self, const fastgltf::Asset &asset, std::size_t nodeIndex, glm::mat4 transform = { 1.f }) -> void {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            transform *= visit(LIFT(fastgltf::toMatrix), node.transform);

            if (node.meshIndex) {
                const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                const glm::dmat4 doublePrecisionTransform = transform;
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    const fastgltf::Accessor &accessor = asset.accessors.at(primitive.findAttribute("POSITION")->second);

                    // TODO: current glTF specification guarantees that there are min/max attributes for POSITION with
                    //  dvec3 type, but KHR_mesh_quantization extension offers non-double precision POSITION attributes,
                    //  which would be problematic in future. Need caution.
                    const std::span min { std::get_if<std::pmr::vector<double>>(&accessor.min)->data(), 3 };
                    const std::span max { std::get_if<std::pmr::vector<double>>(&accessor.max)->data(), 3 };

                    for (int i = 0; i < 8; ++i) {
                        const glm::dvec3 boundingBoxPoint { i & 0b100 ? max[0] : min[0], i & 0b010 ? max[1] : min[1], i & 0b001 ? max[2] : min[2] };
                        const glm::dvec3 transformedPoint = math::toEuclideanCoord(doublePrecisionTransform * glm::dvec4 { boundingBoxPoint, 1.0 });
                        meshBoundingBoxPoints.emplace_back(transformedPoint.x, transformedPoint.y, transformedPoint.z);
                    }
                }
            }

            for (std::size_t childNodeIndex : node.children) {
                self(asset, childNodeIndex, transform);
            }
        };
        for (std::size_t nodeIndex : scene.nodeIndices) {
            traverseSceneNodeRecursive(asset, nodeIndex);
        }

        CGAL::Min_sphere_of_spheres_d<Traits> ms { meshBoundingBoxPoints.begin(), meshBoundingBoxPoints.end() };

        glm::dvec3 center;
        std::copy(ms.center_cartesian_begin(), ms.center_cartesian_end(), value_ptr(center));
        return { center, ms.radius() };
    }

    /**
     * @brief The smallest enclosing sphere of the scene meshes' bounding boxes, i.e. miniball.
     *
     * This overload will use the pre-calculated \p nodeTransforms to make more efficient calculation.
     *
     * @param asset fastgltf Asset.
     * @param scene Scene to be considered. It must be from the same asset.
     * @param nodeWorldTransforms World transforms of the scene nodes. The order must be the same as the scene node indices.
     * @return The pair of the miniball's center and radius.
     */
    [[nodiscard]] std::pair<glm::dvec3, double> getMiniball(const fastgltf::Asset &asset, const fastgltf::Scene &scene, std::span<const glm::mat4> nodeWorldTransforms) {
        // See https://doc.cgal.org/latest/Bounding_volumes/index.html for the original code.
        using Traits = CGAL::Min_sphere_of_points_d_traits_3<CGAL::Simple_cartesian<double>, double>;
        std::vector<Traits::Point> meshBoundingBoxPoints;

        // FIXME: Due to the Clang 18 explicit object parameter bug, redundant const fastgltf::Asset& parameter added.
        const auto traverseSceneNodeRecursive = [&](this const auto &self, const fastgltf::Asset &asset, std::size_t nodeIndex) -> void {
            const fastgltf::Node &node = asset.nodes[nodeIndex];

            if (node.meshIndex) {
                const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                const glm::dmat4 doublePrecisionTransform = nodeWorldTransforms[nodeIndex];
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    const fastgltf::Accessor &accessor = asset.accessors.at(primitive.findAttribute("POSITION")->second);

                    // TODO: current glTF specification guarantees that there are min/max attributes for POSITION with
                    //  dvec3 type, but KHR_mesh_quantization extension offers non-double precision POSITION attributes,
                    //  which would be problematic in future. Need caution.
                    const std::span min { std::get_if<std::pmr::vector<double>>(&accessor.min)->data(), 3 };
                    const std::span max { std::get_if<std::pmr::vector<double>>(&accessor.max)->data(), 3 };

                    for (int i = 0; i < 8; ++i) {
                        const glm::dvec3 boundingBoxPoint { i & 0b100 ? max[0] : min[0], i & 0b010 ? max[1] : min[1], i & 0b001 ? max[2] : min[2] };
                        const glm::dvec3 transformedPoint = math::toEuclideanCoord(doublePrecisionTransform * glm::dvec4 { boundingBoxPoint, 1.0 });
                        meshBoundingBoxPoints.emplace_back(transformedPoint.x, transformedPoint.y, transformedPoint.z);
                    }
                }
            }

            for (std::size_t childNodeIndex : node.children) {
                self(asset, childNodeIndex);
            }
        };
        for (std::size_t nodeIndex : scene.nodeIndices) {
            traverseSceneNodeRecursive(asset, nodeIndex);
        }

        CGAL::Min_sphere_of_spheres_d<Traits> ms { meshBoundingBoxPoints.begin(), meshBoundingBoxPoints.end() };

        glm::dvec3 center;
        std::copy(ms.center_cartesian_begin(), ms.center_cartesian_end(), value_ptr(center));
        return { center, ms.radius() };
    }
}