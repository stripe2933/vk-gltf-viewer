module;

#ifdef EXACT_BOUNDING_VOLUME_USING_CGAL
#include <CGAL/Simple_cartesian.h>
#include <CGAL/Min_sphere_of_points_d_traits_3.h>
#include <CGAL/Min_sphere_of_spheres_d.h>
#endif

export module vk_gltf_viewer.gltf.algorithm.miniball;

import std;
export import fastgltf;

export import vk_gltf_viewer.gltf.AssetExternalBuffers;
import vk_gltf_viewer.helpers.fastgltf;

namespace vk_gltf_viewer::gltf::algorithm {
    /**
     * @brief The smallest enclosing sphere of the scene meshes' bounding boxes, i.e. miniball.
     *
     * @param asset fastgltf Asset.
     * @param sceneIndex Index of scene to be calculated.
     * @param nodeWorldTransforms Node world transform matrices ordered by node indices in the asset.
     * @param adapter Buffer data adapter.
     * @return The tuple of (miniball center, miniball radius, world space positions of camera or light nodes)
     */
    export
    [[nodiscard]] std::tuple<fastgltf::math::fvec3, float, std::vector<fastgltf::math::fvec3>> getMiniball(
        const fastgltf::Asset &asset,
        std::size_t sceneIndex,
        std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
        const AssetExternalBuffers &adapter
    );
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

std::tuple<fastgltf::math::fvec3, float, std::vector<fastgltf::math::fvec3>> vk_gltf_viewer::gltf::algorithm::getMiniball(
    const fastgltf::Asset &asset,
    std::size_t sceneIndex,
    std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
    const AssetExternalBuffers &adapter
) {
#ifdef EXACT_BOUNDING_VOLUME_USING_CGAL
    // See https://doc.cgal.org/latest/Bounding_volumes/index.html for the original code.
    using Traits = CGAL::Min_sphere_of_points_d_traits_3<CGAL::Simple_cartesian<float>, float>;
    std::vector<Traits::Point> scenePoints;
#else
    fastgltf::math::fvec3 min(std::numeric_limits<float>::max());
    fastgltf::math::fvec3 max(std::numeric_limits<float>::lowest());
#endif
    std::vector<fastgltf::math::fvec3> cameraOrLightPoints;

    traverseScene(asset, asset.scenes[sceneIndex], [&](std::size_t nodeIndex) {
        const fastgltf::Node &node = asset.nodes[nodeIndex];
        const fastgltf::math::fmat4x4 &worldTransform = nodeWorldTransforms[nodeIndex];

        // Currently bounding box calculation is performed for both skinned and non-skinned meshes. The result of
        // the former is not exact, completely ignore it will likely lead to a wrong bounding volume.
        // TODO: use skinned mesh bounding volume calculation if available.
        if (node.meshIndex) {
            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            const auto collectTransformedBoundingBoxPoints = [&](const fastgltf::math::fmat4x4 &worldTransform) {
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    for (const fastgltf::math::fvec3 &point : getBoundingBoxCornerPoints(primitive, node, asset)) {
                        const fastgltf::math::fvec3 transformedPoint { worldTransform * fastgltf::math::fvec4 { point.x(), point.y(), point.z(), 1.0 } };

                    #ifdef EXACT_BOUNDING_VOLUME_USING_CGAL
                        scenePoints.emplace_back(transformedPoint.x(), transformedPoint.y(), transformedPoint.z());
                    #else
                        min = cwiseMin(min, transformedPoint);
                        max = cwiseMax(max, transformedPoint);
                    #endif
                    }
                }
            };

            if (node.instancingAttributes.empty()) {
                collectTransformedBoundingBoxPoints(worldTransform);
            }
            else {
                for (const fastgltf::math::fmat4x4 &instanceTransform : getInstanceTransforms(asset, nodeIndex, adapter)) {
                    collectTransformedBoundingBoxPoints(worldTransform * instanceTransform);
                }
            }
        }

        if (node.lightIndex || node.cameraIndex) {
            cameraOrLightPoints.emplace_back(worldTransform.col(3));
        }
    });

#ifdef EXACT_BOUNDING_VOLUME_USING_CGAL
    CGAL::Min_sphere_of_spheres_d<Traits> ms { scenePoints.begin(), scenePoints.end() };

    fastgltf::math::fvec3 center;
    std::copy(ms.center_cartesian_begin(), ms.center_cartesian_end(), center.data());
    return { center, ms.radius(), std::move(cameraOrLightPoints) };
#else
    const fastgltf::math::fvec3 halfDisplacement = (max - min) / 2.f;
    return { min + halfDisplacement, fastgltf::math::length(halfDisplacement), std::move(cameraOrLightPoints) };
#endif
}