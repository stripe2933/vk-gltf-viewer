export module vk_gltf_viewer.math.Plane;

import std;
export import glm;

#ifdef NDEBUG
#define NOEXCEPT_IF_RELEASE noexcept
#else
#define NOEXCEPT_IF_RELEASE
#endif

namespace vk_gltf_viewer::math {
    /**
     * @brief Plane in the 3-dimensional space.
     */
    export struct Plane {
        /**
         * @brief Normal vector of the plane.
         */
        glm::dvec3 normal;

        /**
         * @brief Signed distance of the plane and the origin.
         *
         * The sign is positive if the displacement vector is toward to \p normal, otherwise it is negative.
         */
        double distance;

        /**
         * @brief Get signed distance of the Plane and given \p point.
         *
         * The sign is positive if the displacement vector from plane to \p point is toward to \p normal, otherwise it is negative.
         *
         * @param point Point to get the signed distance for.
         * @return Signed distance.
         */
        [[nodiscard]] double getSignedDistance(const glm::dvec3 &point) const noexcept;

        /**
         * @brief Create Plane from normal and a point that lying on the plane.
         * @param normal Normal vector. MUST be normalized.
         * @param pointInPlane A point that lying on the plane.
         * @return Plane instance.
         */
        [[nodiscard]] static Plane from(const glm::dvec3 &normal, const glm::dvec3 &pointInPlane) noexcept;

        /**
         * @brief Create Plane from coefficients of the plane equation (ax + by + cz + d = 0).
         * @param a Coefficient of x.
         * @param b Coefficient of y.
         * @param c Coefficient of z.
         * @param d Coefficient of the constant term.
         * @return Plane instance.
         */
        [[nodiscard]] static Plane from(double a, double b, double c, double d) noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

double vk_gltf_viewer::math::Plane::getSignedDistance(const glm::dvec3 &point) const noexcept {
    return dot(normal, point) + distance;
}

vk_gltf_viewer::math::Plane vk_gltf_viewer::math::Plane::from(const glm::dvec3 &normal, const glm::dvec3 &pointInPlane) noexcept {
    return { normal, -dot(normal, pointInPlane) };
}

vk_gltf_viewer::math::Plane vk_gltf_viewer::math::Plane::from(double a, double b, double c, double d) noexcept {
    const double inverseLength = 1.f / std::hypot(a, b, c);
    return { inverseLength * glm::dvec3 { a, b, c }, inverseLength * d };
}