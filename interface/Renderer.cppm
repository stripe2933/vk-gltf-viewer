module;

#include <boost/container/static_vector.hpp>

export module vk_gltf_viewer.Renderer;

import std;
export import glm;
export import imgui.internal;
export import ImGuizmo;

export import vk_gltf_viewer.control.Camera;
export import vk_gltf_viewer.helpers.full_optional;

namespace vk_gltf_viewer {
    export class Renderer {
    public:
        struct Capabilities {
            /**
             * @brief Indicates whether the renderer supports per-fragment bloom effect.
             *
             * In Vulkan, <tt>VK_EXT_shader_stencil_export</tt> is required for this.
             */
            bool perFragmentBloom;
        };

        struct Outline {
            /**
             * @brief Outline thickness in pixels. Must be greater than 0.
             */
            float thickness;

            /**
             * @brief Outline color in RGBA format.
             */
            glm::vec4 color;
        };

        struct Bloom {
            enum Mode : std::uint8_t {
                /// Material which has <tt>KHR_materials_emissive_strength</tt> extension is rendered with bloom effect.
                PerMaterial,
                /// Fragment whose maximum of emissive RGB values is greater than 1 is rendered with bloom effect.
                PerFragment,
            };

            /**
             * @brief Bloom mode.
             */
            Mode mode;

            /**
             * @brief Multiplier used in bloom composition.
             */
            float intensity;
        };

        enum class FrustumCullingMode : std::uint8_t {
            /// Frustum culling is disabled.
            Off,
            /// Frustum culling is enabled, but node with <tt>EXT_mesh_gpu_instancing</tt> extension is not considered
            /// for culling.
            On,
            /// Frustum culling is enabled, and node with <tt>EXT_mesh_gpu_instancing</tt> extension is culled when all
            /// instances are outside the frustum.s
            OnWithInstancing,
        };

        Capabilities capabilities;

        boost::container::static_vector<control::Camera, 4> cameras {
            {
                glm::vec3 { 0.f, 0.f, 5.f }, glm::vec3 { 0.f, 0.f, -1.f }, glm::vec3 { 0.f, 1.f, 0.f },
                glm::radians(45.f), 1.f /* will be determined by passthru rect dimension */, 1e-2f, 10.f,
                5.f,
            },
            {
                glm::vec3 { 5.f, 0.f, 0.f }, glm::vec3 { -1.f, 0.f, 0.f }, glm::vec3 { 0.f, 1.f, 0.f },
                glm::radians(45.f), 1.f /* will be determined by passthru rect dimension */, 1e-2f, 10.f,
                5.f,
            },
            {
                glm::vec3 { -0.3926829328, 4.9845561602, 0.f }, glm::vec3 { 0.01570731731f, -0.9998766325f, 0.f }, glm::vec3 { 0.f, 1.f, 0.f },
                glm::radians(45.f), 1.f /* will be determined by passthru rect dimension */, 1e-2f, 10.f,
                5.f,
            },
            {
                glm::vec3 { 2.8867513459f /* = 5/sqrt(3) */ }, glm::vec3 { -0.5773502692f /* = -1/sqrt(3) */ }, glm::vec3 { 0.f, 1.f, 0.f },
                glm::radians(45.f), 1.f /* will be determined by passthru rect dimension */, 1e-2f, 10.f,
                5.f,
            },
        };

        std::size_t imGuiSelectedCameraIndex = 0;

        /**
         * @brief Boolean flag indicating whether the renderer should automatically adjust near and far planes based on
         * the scene bounding box.
         */
        bool automaticNearFarPlaneAdjustment = true;

        /**
         * @brief Background color, or <tt>std::nullopt</tt> if the renderer should use skybox.
         */
        full_optional<glm::vec3> solidBackground { std::in_place, 0.f, 0.f, 0.f };

        /**
         * @brief Outline color and thickness for hovering node, or <tt>std::nullopt</tt> if outline drawing is disabled.
         */
        full_optional<Outline> hoveringNodeOutline { std::in_place, 2.f, glm::vec4 { 1.f, 0.5f, 0.2f, 1.f } };

        /**
         * @brief Outline color and thickness for selected nodes, or <tt>std::nullopt</tt> if outline drawing is disabled.
         */
        full_optional<Outline> selectedNodeOutline { std::in_place, 2.f, glm::vec4 { 0.f, 1.f, 0.2f, 1.f } };

        /**
         * @brief ImGuizmo operation.
         */
        ImGuizmo::OPERATION imGuizmoOperation = ImGuizmo::OPERATION::TRANSLATE;

        /**
         * @brief Bloom mode.
         *
         * If <tt>capabilities.perFragmentBloom</tt> is <tt>false</tt>, <tt>Bloom::PerFragment</tt> is not allowed.
         */
        full_optional<Bloom> bloom { unset, Bloom::PerMaterial, 0.04f };

        /**
         * @brief Frustum culling mode.
         */
        FrustumCullingMode frustumCullingMode = FrustumCullingMode::Off;

        explicit Renderer(const Capabilities &capabilities)
            : capabilities { capabilities }
            , _canSelectSkyboxBackground { false } { }

        [[nodiscard]] boost::container::static_vector<ImRect, 4> getCameraRects(const ImRect &passthruRect) const noexcept;
        [[nodiscard]] ImVec2 getCameraExtent(const ImVec2 &passthruExtent) const noexcept;
        [[nodiscard]] float getCameraAspectRatio(const ImVec2 &passthruExtent) const noexcept;

        [[nodiscard]] bool canSelectSkyboxBackground() const noexcept;
        void setSkybox(std::monostate) noexcept;

    private:
        bool _canSelectSkyboxBackground;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

boost::container::static_vector<ImRect, 4> vk_gltf_viewer::Renderer::getCameraRects(const ImRect &passthruRect) const noexcept {
    switch (cameras.size()) {
        case 1:
            // +-------------------+
            // |                   |
            // |         0         |
            // |                   |
            // +-------------------+
            return { passthruRect };
        case 2: {
            // +---------+---------+
            // |         |         |
            // |    0    |    1    |
            // |         |         |
            // +---------+---------+
            const float halfWidth = passthruRect.GetWidth() / 2.f;
            return {
                { passthruRect.Min.x, passthruRect.Min.y, passthruRect.Min.x + halfWidth, passthruRect.Max.y },
                { passthruRect.Min.x + halfWidth, passthruRect.Min.y, passthruRect.Max.x, passthruRect.Max.y },
            };
        }
        case 4: {
            // +---------+---------+
            // |    0    |    1    |
            // +---------+---------+
            // |    2    |    3    |
            // +---------+---------+
            const float halfWidth = passthruRect.GetWidth() / 2.f;
            const float halfHeight = passthruRect.GetHeight() / 2.f;
            return {
                { passthruRect.Min.x, passthruRect.Min.y, passthruRect.Min.x + halfWidth, passthruRect.Min.y + halfHeight },
                { passthruRect.Min.x + halfWidth, passthruRect.Min.y, passthruRect.Max.x, passthruRect.Min.y + halfHeight },
                { passthruRect.Min.x, passthruRect.Min.y + halfHeight, passthruRect.Min.x + halfWidth, passthruRect.Max.y },
                { passthruRect.Min.x + halfWidth, passthruRect.Min.y + halfHeight, passthruRect.Max.x, passthruRect.Max.y },
            };
        }
        default:
            std::unreachable();
    }
}

ImVec2 vk_gltf_viewer::Renderer::getCameraExtent(const ImVec2 &passthruExtent) const noexcept {
    switch (cameras.size()) {
        case 1:
            return passthruExtent;
        case 2:
            return { passthruExtent.x / 2.f, passthruExtent.y };
        case 4:
            return { passthruExtent.x / 2.f, passthruExtent.y / 2.f };
        default:
            std::unreachable();
    }
}

float vk_gltf_viewer::Renderer::getCameraAspectRatio(const ImVec2 &passthruExtent) const noexcept {
    switch (cameras.size()) {
        case 1: case 4:
            return passthruExtent.x / passthruExtent.y;
        case 2:
            return passthruExtent.x / (passthruExtent.y / 2.f);
        default:
            std::unreachable();
    }
}

bool vk_gltf_viewer::Renderer::canSelectSkyboxBackground() const noexcept {
    return _canSelectSkyboxBackground;
}

void vk_gltf_viewer::Renderer::setSkybox(std::monostate) noexcept {
    solidBackground.reset();
    _canSelectSkyboxBackground = true;
}