export module vk_gltf_viewer.control.Camera;

import std;
export import glm;

import vk_gltf_viewer.helpers.functional;
export import vk_gltf_viewer.math.Frustum;

namespace vk_gltf_viewer::control {
    export struct Camera {
        struct Perspective {
            float yfov;
        };

        struct Orthographic {
            float ymag;
        };

        glm::vec3 position;
        glm::vec3 direction;
        glm::vec3 up;

        std::variant<Perspective, Orthographic> projection;
        float zMin;
        float zMax;

        /**
         * A distance to be used for <tt>ImGuizmo::ViewManipulate</tt>, which is the distance between pivot and
         * <tt>position</tt>.
         */
        float targetDistance;

        /**
         * @brief If <tt>true</tt>, the renderer will automatically adjust the near and far planes based on the scene
         * bounding box.
         */
        bool automaticNearFarPlaneAdjustment = true;

        [[nodiscard]] glm::mat4 getViewMatrix() const noexcept;
        [[nodiscard]] glm::mat4 getProjectionMatrix(float aspectRatio) const noexcept;
        [[nodiscard]] glm::mat4 getProjectionMatrixForwardZ(float aspectRatio) const noexcept;
        [[nodiscard]] glm::mat4 getProjectionViewMatrix(float aspectRatio) const noexcept;
        [[nodiscard]] glm::mat4 getProjectionViewMatrixForwardZ(float aspectRatio) const noexcept;
        [[nodiscard]] glm::vec3 getRight() const noexcept;

        [[nodiscard]] float getEquivalentYFov() const noexcept;
        [[nodiscard]] float getEquivalentYMag() const noexcept;

        void adjustMiniball(const glm::vec3 &center, float radius, float aspectRatio);
        void tightenNearFar(const glm::vec3 &boundingSphereCenter, float boundingSphereRadius) noexcept;

        [[nodiscard]] math::Frustum getFrustum(float aspectRatio) const;
        [[nodiscard]] math::Frustum getFrustum(float aspectRatio, float xmin, float xmax, float ymin, float ymax) const;
    };
}

#ifndef __GNUC
module :private;
#endif

glm::mat4 vk_gltf_viewer::control::Camera::getViewMatrix() const noexcept {
    return lookAt(position, position + direction, up);
}

glm::mat4 vk_gltf_viewer::control::Camera::getProjectionMatrix(float aspectRatio) const noexcept {
    return visit(multilambda {
        [&](const Perspective &perspective) noexcept {
            return glm::perspectiveRH_ZO(perspective.yfov, aspectRatio, zMax, zMin);
        },
        [&](const Orthographic &orthographic) noexcept {
            const float halfYmag = orthographic.ymag / 2.f;
            const float halfXmag = halfYmag * aspectRatio;
            return glm::orthoRH_ZO(-halfXmag, halfXmag, -halfYmag, halfYmag, zMax, zMin);
        },
    }, projection);
}

glm::mat4 vk_gltf_viewer::control::Camera::getProjectionMatrixForwardZ(float aspectRatio) const noexcept {
    return visit(multilambda {
        [&](const Perspective &perspective) noexcept {
            return glm::perspectiveRH_ZO(perspective.yfov, aspectRatio, zMin, zMax);
        },
        [&](const Orthographic &orthographic) noexcept {
            const float halfYmag = orthographic.ymag / 2.f;
            const float halfXmag = halfYmag * aspectRatio;
            return glm::orthoRH_ZO(-halfXmag, halfXmag, -halfYmag, halfYmag, zMin, zMax);
        },
    }, projection);
}

glm::mat4 vk_gltf_viewer::control::Camera::getProjectionViewMatrix(float aspectRatio) const noexcept {
    return getProjectionMatrix(aspectRatio) * getViewMatrix();
}

glm::mat4 vk_gltf_viewer::control::Camera::getProjectionViewMatrixForwardZ(float aspectRatio) const noexcept {
    return getProjectionMatrixForwardZ(aspectRatio) * getViewMatrix();
}

glm::vec3 vk_gltf_viewer::control::Camera::getRight() const noexcept {
    return cross(direction, up);
}

float vk_gltf_viewer::control::Camera::getEquivalentYFov() const noexcept {
    return visit(multilambda {
        [](const Perspective &perspective) noexcept {
            return perspective.yfov;
        },
        [this](const Orthographic &orthographic) noexcept {
            const float zMid = std::midpoint(zMin, zMax);
            return 2.f * std::atan2(orthographic.ymag / 2.f, zMid);
        },
    }, projection);
}

float vk_gltf_viewer::control::Camera::getEquivalentYMag() const noexcept {
    return visit(multilambda {
        [this](const Perspective &perspective) noexcept {
            const float zMid = std::midpoint(zMin, zMax);
            return 2.f * zMid * std::tan(perspective.yfov / 2.f);
        },
        [](const Orthographic &orthographic) noexcept {
            return orthographic.ymag;
        },
    }, projection);
}

void vk_gltf_viewer::control::Camera::adjustMiniball(const glm::vec3 &center, float radius, float aspectRatio) {
    visit(multilambda {
        [&](const Perspective &perspective) noexcept {
            float s = std::sin(perspective.yfov / 2.f);
            if (aspectRatio < 1.f) {
                // zMin * sin(yfov / 2) = nearPlaneHeight / 2
                // zMin * sin(xfov / 2) = nearPlaneWidth / 2
                // -> sin(xfov / 2) / sin(yfov / 2) = nearPlaneWidth / nearPlaneHeight = asepctRatio
                // -> sin(xfov / 2) = aspectRatio * sin(yfov / 2)
                s *= aspectRatio;
            }

            const float distance = radius / s;
            position = center - distance * normalize(direction);
            zMin = distance - radius;
            zMax = distance + radius;
            targetDistance = distance;
        },
        [&](Orthographic &orthographic) noexcept {
            const float distance = 2.f * radius;
            position = center - distance * normalize(direction);

            orthographic.ymag = 2.f * radius;
            if (aspectRatio < 1.f) {
                orthographic.ymag /= aspectRatio;
            }

            zMin = radius;
            zMax = 3.f * radius;
            targetDistance = distance;
        },
    }, projection);

}

void vk_gltf_viewer::control::Camera::tightenNearFar(const glm::vec3 &boundingSphereCenter, float boundingSphereRadius) noexcept {
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

vk_gltf_viewer::math::Frustum vk_gltf_viewer::control::Camera::getFrustum(float aspectRatio) const {
    // Gribb & Hartmann method.
    const glm::mat4 m = getProjectionViewMatrixForwardZ(aspectRatio);
    return {
        math::Plane::from(m[0].w + m[0].z, m[1].w + m[1].z, m[2].w + m[2].z, m[3].w + m[3].z), // Near
        math::Plane::from(m[0].w - m[0].z, m[1].w - m[1].z, m[2].w - m[2].z, m[3].w - m[3].z), // Far
        math::Plane::from(m[0].w + m[0].x, m[1].w + m[1].x, m[2].w + m[2].x, m[3].w + m[3].x), // Left
        math::Plane::from(m[0].w - m[0].x, m[1].w - m[1].x, m[2].w - m[2].x, m[3].w - m[3].x), // Right
        math::Plane::from(m[0].w - m[0].y, m[1].w - m[1].y, m[2].w - m[2].y, m[3].w - m[3].y), // Top
        math::Plane::from(m[0].w + m[0].y, m[1].w + m[1].y, m[2].w + m[2].y, m[3].w + m[3].y), // Bottom
    };
}

vk_gltf_viewer::math::Frustum vk_gltf_viewer::control::Camera::getFrustum(float aspectRatio, float xmin, float xmax, float ymin, float ymax) const {
    // Gribb & Hartmann method.
    const glm::mat4 m
        = visit(multilambda {
            [&](const Perspective &perspective) noexcept {
                const float nearHeight = 2.f * zMin * std::tan(perspective.yfov / 2.f);
                const float nearWidth = nearHeight * aspectRatio;
                return glm::gtc::frustum(
                    nearWidth * (xmin - 0.5f), nearWidth * (xmax - 0.5f),
                    nearHeight * (ymin - 0.5f), nearHeight * (ymax - 0.5f),
                    zMin, zMax);
            },
            [&](const Orthographic &orthographic) noexcept {
                const float halfYmag = orthographic.ymag / 2.f;
                const float halfXmag = halfYmag * aspectRatio;
                const float xmag = halfXmag * 2.f;
                return glm::orthoRH_ZO(-halfXmag + xmag * xmin, -halfXmag + xmag * xmax, -halfYmag + orthographic.ymag * ymin, halfYmag + orthographic.ymag * ymax, zMin, zMax);
            },
        }, projection)
        * getViewMatrix();
    return {
        math::Plane::from(m[0].w + m[0].z, m[1].w + m[1].z, m[2].w + m[2].z, m[3].w + m[3].z), // Near
        math::Plane::from(m[0].w - m[0].z, m[1].w - m[1].z, m[2].w - m[2].z, m[3].w - m[3].z), // Far
        math::Plane::from(m[0].w + m[0].x, m[1].w + m[1].x, m[2].w + m[2].x, m[3].w + m[3].x), // Left
        math::Plane::from(m[0].w - m[0].x, m[1].w - m[1].x, m[2].w - m[2].x, m[3].w - m[3].x), // Right
        math::Plane::from(m[0].w - m[0].y, m[1].w - m[1].y, m[2].w - m[2].y, m[3].w - m[3].y), // Top
        math::Plane::from(m[0].w + m[0].y, m[1].w + m[1].y, m[2].w + m[2].y, m[3].w + m[3].y), // Bottom
    };
}