export module vk_gltf_viewer:control.Camera;

import std;
export import glm;
export import :math.Frustum;

namespace vk_gltf_viewer::control {
    export struct Camera {
        glm::vec3 position;
        glm::vec3 direction;
        glm::vec3 up;

        float fov;
        float aspectRatio;
        float zMin;
        float zMax;

        /**
         * A distance to be used for <tt>ImGuizmo::ViewManipulate</tt>, which is the distance between pivot and
         * <tt>position</tt>.
         */
        float targetDistance;

        [[nodiscard]] glm::mat4 getViewMatrix() const noexcept {
            return lookAt(position, position + direction, up);
        }

        [[nodiscard]] glm::mat4 getProjectionMatrix() const noexcept {
            return glm::perspectiveRH_ZO(fov, aspectRatio, zMax, zMin);
        }

        [[nodiscard]] glm::mat4 getProjectionMatrixForwardZ() const noexcept {
            return glm::perspectiveRH_ZO(fov, aspectRatio, zMin, zMax);
        }

        [[nodiscard]] glm::mat4 getProjectionViewMatrix() const noexcept {
            return getProjectionMatrix() * getViewMatrix();
        }

        [[nodiscard]] glm::mat4 getProjectionViewMatrixForwardZ() const noexcept {
            return getProjectionMatrixForwardZ() * getViewMatrix();
        }

        [[nodiscard]] constexpr glm::vec3 getRight() const noexcept {
            return cross(direction, up);
        }

        void tightenNearFar(const glm::vec3 &boundingSphereCenter, float boundingSphereRadius) noexcept {
            // Get projection of the displacement vector (from camera position to bounding sphere center) on the direction vector.
            const glm::vec3 displacement = boundingSphereCenter - position;
            const float displacementProjectionLength = dot(displacement, direction);
            const float displacementNearProjectionLength = displacementProjectionLength - boundingSphereRadius;
            const float displacementFarProjectionLength = displacementProjectionLength + boundingSphereRadius;
            if (displacementFarProjectionLength <= 0.f) {
                // The bounding sphere is behind the camera.
                zMin = 1e-2f;
                zMax = 1e2f;
            }
            else {
                zMin = std::max(1e-2f, displacementNearProjectionLength);
                zMax = displacementFarProjectionLength;
            }
        }

        [[nodiscard]] math::Frustum getFrustum() const {
            // Gribb & Hartmann method.
            const glm::mat4 m = getProjectionViewMatrixForwardZ();
            return {
                math::Plane::from(m[0].w + m[0].z, m[1].w + m[1].z, m[2].w + m[2].z, m[3].w + m[3].z), // Near
                math::Plane::from(m[0].w - m[0].z, m[1].w - m[1].z, m[2].w - m[2].z, m[3].w - m[3].z), // Far
                math::Plane::from(m[0].w + m[0].x, m[1].w + m[1].x, m[2].w + m[2].x, m[3].w + m[3].x), // Left
                math::Plane::from(m[0].w - m[0].x, m[1].w - m[1].x, m[2].w - m[2].x, m[3].w - m[3].x), // Right
                math::Plane::from(m[0].w - m[0].y, m[1].w - m[1].y, m[2].w - m[2].y, m[3].w - m[3].y), // Top
                math::Plane::from(m[0].w + m[0].y, m[1].w + m[1].y, m[2].w + m[2].y, m[3].w + m[3].y), // Bottom
            };
        }
    };
}