export module vk_gltf_viewer:control.Camera;

export import glm;

namespace vk_gltf_viewer::control {
    export struct Camera {
        glm::vec3 position;
        glm::vec3 direction;
        glm::vec3 up;

        float fov;
        float aspectRatio;
        float zMin;
        float zMax;

        [[nodiscard]] auto getViewMatrix() const noexcept -> glm::mat4 {
            return lookAt(position, position + direction, up);
        }

        [[nodiscard]] auto getProjectionMatrix() const noexcept -> glm::mat4 {
            return glm::perspectiveRH_ZO(fov, aspectRatio, zMax, zMin);
        }

        [[nodiscard]] auto getProjectionViewMatrix() const noexcept -> glm::mat4 {
            return getProjectionMatrix() * getViewMatrix();
        }

        [[nodiscard]] constexpr auto getRight() const noexcept -> glm::vec3 {
            return cross(direction, up);
        }
    };
}