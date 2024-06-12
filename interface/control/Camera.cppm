export module vk_gltf_viewer:control.Camera;

export import glm;

namespace vk_gltf_viewer::control {
    export struct Camera {
        struct ViewDecomposition {
            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 front;
            glm::vec3 eye;
        };

        glm::mat4 view;
        glm::mat4 projection;

        [[nodiscard]] auto getRight() const noexcept -> glm::vec3 {
            return getRight(inverse(view));
        }

        [[nodiscard]] auto getUp() const noexcept -> glm::vec3 {
            return getUp(inverse(view));
        }

        [[nodiscard]] auto getFront() const noexcept -> glm::vec3 {
            return getFront(inverse(view));
        }

        [[nodiscard]] auto getEye() const noexcept -> glm::vec3 {
            return getEye(inverse(view));
        }

        [[nodiscard]] auto getViewDecomposition() const noexcept -> ViewDecomposition {
            const glm::mat4 inverseView = inverse(view);
            return {
                getRight(inverseView),
                getUp(inverseView),
                getFront(inverseView),
                getEye(inverseView),
            };
        }

        [[nodiscard]] constexpr auto getAspectRatio() const noexcept -> float {
            return projection[1][1] / projection[0][0];
        }

        [[nodiscard]] auto getFov() const noexcept -> float {
            return 2.f * glm::atan(1.f / projection[1][1]);
        }

        [[nodiscard]] constexpr auto getNear() const noexcept -> float {
            return
    #ifdef GLM_FORCE_DEPTH_ZERO_TO_ONE
                2.f *
    #endif
                projection[3][2] / (projection[2][2] - 1.0);
        }

        [[nodiscard]] constexpr auto getFar() const noexcept -> float {
            return projection[3][2] / (projection[2][2] + 1.0);
        }

        [[nodiscard]] static constexpr auto getRight(const glm::mat4& inverseView) noexcept -> glm::vec3 {
            return inverseView[0];
        }

        [[nodiscard]] static constexpr auto getUp(const glm::mat4& inverseView) noexcept -> glm::vec3 {
            return inverseView[1];
        }

        [[nodiscard]] static constexpr auto getFront(const glm::mat4& inverseView) noexcept -> glm::vec3 {
            return -inverseView[2];
        }

        [[nodiscard]] static constexpr auto getEye(const glm::mat4& inverseView) noexcept -> glm::vec3 {
            return inverseView[3];
        }
    };
}