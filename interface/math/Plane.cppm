export module vk_gltf_viewer:math.Plane;

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
    struct Plane {
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
        [[nodiscard]] constexpr float getSignedDistance(const glm::vec3 &point) const noexcept {
            return dot(normal, point) + distance;;
        }

        /**
         * @brief Create Plane from normal and a point that lying on the plane.
         * @param normal Normal vector. MUST be normalized.
         * @param pointInPlane A point that lying on the plane.
         * @return Plane instance.
         */
        [[nodiscard]] static constexpr Plane from(const glm::vec3 &normal, const glm::vec3 &pointInPlane) noexcept {
            return { normal, -dot(normal, pointInPlane) };
        };
    };
}