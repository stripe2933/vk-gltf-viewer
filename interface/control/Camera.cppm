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

        [[nodiscard]] auto getViewMatrix() const noexcept -> glm::mat4 {
            return lookAt(position, position + direction, up);
        }

        [[nodiscard]] auto getProjectionMatrix() const noexcept -> glm::mat4 {
            return glm::perspectiveRH_ZO(fov, aspectRatio, zMax, zMin);
        }

        [[nodiscard]] auto getProjectionMatrixForwardZ() const noexcept -> glm::mat4 {
            return glm::perspectiveRH_ZO(fov, aspectRatio, zMin, zMax);
        }

        [[nodiscard]] auto getProjectionViewMatrix() const noexcept -> glm::mat4 {
            return getProjectionMatrix() * getViewMatrix();
        }

        [[nodiscard]] auto getProjectionViewMatrixForwardZ() const noexcept -> glm::mat4 {
            return getProjectionMatrixForwardZ() * getViewMatrix();
        }

        [[nodiscard]] constexpr auto getRight() const noexcept -> glm::vec3 {
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

        [[nodiscard]] auto getFrustum() const -> math::Frustum {
            // Code from LearnOpenGL.
            // See: https://learnopengl.com/Guest-Articles/2021/Scene/Frustum-Culling.
            const float halfVSide = zMax * std::tan(fov / 2.f);
            const float halfHSide = halfVSide * aspectRatio;
            const glm::vec3 frontMultFar = direction * zMax;
            const glm::vec3 right = getRight();
            const glm::vec3 rightMultHalfHSide = right * halfHSide;
            const glm::vec3 upMultHalfVSide = up * halfVSide;

            return {
                math::Plane::from(direction, position + zMin * direction),
                math::Plane::from(-direction, position + frontMultFar),
                math::Plane::from(normalize(cross(frontMultFar - rightMultHalfHSide, up)), position),
                math::Plane::from(normalize(cross(up, frontMultFar + rightMultHalfHSide)), position),
                math::Plane::from(normalize(cross(frontMultFar + upMultHalfVSide, right)), position),
                math::Plane::from(normalize(cross(right, frontMultFar - upMultHalfVSide)), position),
            };
        }
    };
}