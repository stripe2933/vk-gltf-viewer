export module vk_gltf_viewer:math.Frustum;

import std;
export import :math.Plane;

namespace vk_gltf_viewer::math {
    /**
     * @brief Frustum in the 3-dimensional space.
     */
    export struct Frustum {
        /**
         * @brief Planes of the frustum.
         *
         * Order is: near, far, left, right, top, bottom.
         */
        std::array<Plane, 6> planes;

        /**
         * @brief Determine if the sphere is on the frustum, with an approximated method.
         *
         * This may determine that the sphere is overlapping with the frustum even if it is not (false-positive) when
         * the sphere is close to the edge or the corner of the frustum. However, it does not determine that the sphere
         * is not overlapping with the frustum when it is (true-negative).
         *
         * @param center Center of the sphere.
         * @param radius Radius of the sphere.
         * @return <tt>true</tt> if the sphere is overlapping with the frustum, <tt>false</tt> otherwise.
         */
        [[nodiscard]] constexpr bool isOverlapApprox(const glm::vec3 &center, float radius) const noexcept {
            for (const Plane &plane : planes) {
                if (plane.getSignedDistance(center) < -radius) {
                    return false;
                }
            }

            return true;
        }
    };
}