export module vk_gltf_viewer.control.Camera;

import std;
export import glm;

export import vk_gltf_viewer.math.Frustum;

namespace vk_gltf_viewer::control {
    export struct Camera {
        glm::dvec3 position;
        glm::vec3 direction;
        glm::vec3 up;

        float fov;
        float aspectRatio;
        double zMin;
        double zMax;

        /**
         * A distance to be used for <tt>ImGuizmo::ViewManipulate</tt>, which is the distance between pivot and
         * <tt>position</tt>.
         */
        double targetDistance;

        /**
         * @brief If <tt>true</tt>, the renderer will automatically adjust the near and far planes based on the scene
         * bounding box.
         */
        bool automaticNearFarPlaneAdjustment = true;

        [[nodiscard]] glm::mat4 getViewMatrix() const noexcept;
        [[nodiscard]] glm::mat4 getProjectionMatrix() const noexcept;
        [[nodiscard]] glm::mat4 getProjectionMatrixForwardZ() const noexcept;
        [[nodiscard]] glm::mat4 getProjectionViewMatrix() const noexcept;
        [[nodiscard]] glm::mat4 getProjectionViewMatrixForwardZ() const noexcept;
        [[nodiscard]] glm::vec3 getRight() const noexcept;

        void tightenNearFar(const glm::dvec3 &boundingSphereCenter, double boundingSphereRadius) noexcept;

        [[nodiscard]] math::Frustum getFrustum() const;
        [[nodiscard]] math::Frustum getFrustum(float xmin, float xmax, float ymin, float ymax) const;
    };
}

#ifndef __GNUC
module :private;
#endif

glm::mat4 vk_gltf_viewer::control::Camera::getViewMatrix() const noexcept {
    return lookAt(position, position + glm::dvec3 { direction }, glm::dvec3 { up });
}

glm::mat4 vk_gltf_viewer::control::Camera::getProjectionMatrix() const noexcept {
    return glm::perspectiveRH_ZO(fov, aspectRatio, static_cast<float>(zMax), static_cast<float>(zMin));
}

glm::mat4 vk_gltf_viewer::control::Camera::getProjectionMatrixForwardZ() const noexcept {
    return glm::perspectiveRH_ZO(fov, aspectRatio, static_cast<float>(zMin), static_cast<float>(zMax));
}

glm::mat4 vk_gltf_viewer::control::Camera::getProjectionViewMatrix() const noexcept {
    return getProjectionMatrix() * getViewMatrix();
}

glm::mat4 vk_gltf_viewer::control::Camera::getProjectionViewMatrixForwardZ() const noexcept {
    return getProjectionMatrixForwardZ() * getViewMatrix();
}

glm::vec3 vk_gltf_viewer::control::Camera::getRight() const noexcept {
    return cross(direction, up);
}

void vk_gltf_viewer::control::Camera::tightenNearFar(const glm::dvec3 &boundingSphereCenter, double boundingSphereRadius) noexcept {
    // Get projection of the displacement vector (from camera position to bounding sphere center) on the direction vector.
    const glm::dvec3 displacement = boundingSphereCenter - position;
    const double displacementProjectionLength = dot(displacement, glm::dvec3 { direction });
    const double displacementNearProjectionLength = displacementProjectionLength - boundingSphereRadius;
    const double displacementFarProjectionLength = displacementProjectionLength + boundingSphereRadius;
    if (displacementFarProjectionLength <= 0.0) {
        // The bounding sphere is behind the camera.
        zMin = 1e-2;
        zMax = 1e2;
    }
    else {
        zMin = std::max(1e-2, displacementNearProjectionLength);
        zMax = displacementFarProjectionLength;
    }
}

vk_gltf_viewer::math::Frustum vk_gltf_viewer::control::Camera::getFrustum() const {
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

vk_gltf_viewer::math::Frustum vk_gltf_viewer::control::Camera::getFrustum(float xmin, float xmax, float ymin, float ymax) const {
    const double nearWidth = 2.0 * zMin * std::tan(fov / 2.f);
    const double nearHeight = nearWidth * aspectRatio;
    const glm::mat4 projection = glm::gtc::frustum<float>(
        static_cast<float>(nearWidth * (xmin - 0.5f)), static_cast<float>(nearWidth * (xmax - 0.5f)),
        static_cast<float>(nearHeight * (ymin - 0.5f)), static_cast<float>(nearHeight * (ymax - 0.5f)),
        static_cast<float>(zMin), static_cast<float>(zMax));

    // Gribb & Hartmann method.
    const glm::mat4 m = projection * getViewMatrix();
    return {
        math::Plane::from(m[0].w + m[0].z, m[1].w + m[1].z, m[2].w + m[2].z, m[3].w + m[3].z), // Near
        math::Plane::from(m[0].w - m[0].z, m[1].w - m[1].z, m[2].w - m[2].z, m[3].w - m[3].z), // Far
        math::Plane::from(m[0].w + m[0].x, m[1].w + m[1].x, m[2].w + m[2].x, m[3].w + m[3].x), // Left
        math::Plane::from(m[0].w - m[0].x, m[1].w - m[1].x, m[2].w - m[2].x, m[3].w - m[3].x), // Right
        math::Plane::from(m[0].w - m[0].y, m[1].w - m[1].y, m[2].w - m[2].y, m[3].w - m[3].y), // Top
        math::Plane::from(m[0].w + m[0].y, m[1].w + m[1].y, m[2].w + m[2].y, m[3].w + m[3].y), // Bottom
    };
}