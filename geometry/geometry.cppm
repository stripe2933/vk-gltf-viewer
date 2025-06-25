export module geometry;

import std;
export import glm;

namespace geometry {
    /**
     * @brief Plane in the 3-dimensional space.
     */
    export struct Plane {
        /**
         * @brief Normal vector of the plane.
         */
        glm::vec3 normal;

        /**
         * @brief Signed distance of the plane and the origin.
         *
         * The sign is positive if the displacement vector is toward to \p normal, otherwise it is negative.
         */
        float distance;

        /**
         * @brief Get signed distance of the Plane and given \p point.
         *
         * The sign is positive if the displacement vector from plane to \p point is toward to \p normal, otherwise it is negative.
         *
         * @param point Point to get the signed distance for.
         * @return Signed distance.
         */
        [[nodiscard]] float getSignedDistance(const glm::vec3 &point) const noexcept;

        /**
         * @brief Create Plane from normal and a point that lying on the plane.
         * @param normal Normal vector. MUST be normalized.
         * @param pointInPlane A point that lying on the plane.
         * @return Plane instance.
         */
        [[nodiscard]] static Plane from(const glm::vec3 &normal, const glm::vec3 &pointInPlane) noexcept;
    };

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
        [[nodiscard]] bool isOverlapApprox(const glm::vec3 &center, float radius) const noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

float geometry::Plane::getSignedDistance(const glm::vec3 &point) const noexcept {
    return dot(normal, point) + distance;
}

geometry::Plane geometry::Plane::from(const glm::vec3 &normal, const glm::vec3 &pointInPlane) noexcept {
    return { normal, -dot(normal, pointInPlane) };
}

bool geometry::Frustum::isOverlapApprox(const glm::vec3 &center, float radius) const noexcept {
    for (const Plane &plane : planes) {
        if (plane.getSignedDistance(center) < -radius) {
            return false;
        }
    }

    return true;
}
