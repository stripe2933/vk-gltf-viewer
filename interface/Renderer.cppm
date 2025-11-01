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

        struct Grid {
            /// Grid line color
            glm::vec3 color;

            /// Grid size
            float size;

            /// Whether to show minor axis lines
            bool showMinorAxes;
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
            control::Camera {
                glm::vec3 { 0.f, 0.f, 5.f }, normalize(glm::vec3 { 0.f, 0.f, -1.f }), glm::vec3 { 0.f, 1.f, 0.f },
                glm::radians(45.f), 1.f /* will be determined by viewport extent */, 1e-2f, 10.f,
                5.f,
            }
        };

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

        full_optional<Grid> grid { unset, glm::vec3 { 0.25f }, 100.f, true };

        /**
         * @brief Frustum culling mode.
         */
        FrustumCullingMode frustumCullingMode = FrustumCullingMode::OnWithInstancing;

        explicit Renderer(const Capabilities &capabilities)
            : capabilities { capabilities }
            , _canSelectSkyboxBackground { false } { }

        [[nodiscard]] bool canSelectSkyboxBackground() const noexcept;
        void setSkybox(std::monostate) noexcept;

        [[nodiscard]] ImRect getViewportRect(const ImRect &passthruRect, std::size_t viewIndex) const noexcept;
        [[nodiscard]] boost::container::static_vector<ImRect, 4> getViewportRects(const ImRect &passthruRect) const noexcept;

    private:
        bool _canSelectSkyboxBackground;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

bool vk_gltf_viewer::Renderer::canSelectSkyboxBackground() const noexcept {
    return _canSelectSkyboxBackground;
}

void vk_gltf_viewer::Renderer::setSkybox(std::monostate) noexcept {
    solidBackground.reset();
    _canSelectSkyboxBackground = true;
}

ImRect vk_gltf_viewer::Renderer::getViewportRect(const ImRect &passthruRect, std::size_t viewIndex) const noexcept {
    assert(viewIndex < cameras.size());

    switch (cameras.size()) {
        case 1:
            return passthruRect;
        case 2:
            if (viewIndex == 0) {
                return { passthruRect.Min, { std::midpoint(passthruRect.Min.x, passthruRect.Max.x), passthruRect.Max.y } };
            }
            else {
                return { { std::midpoint(passthruRect.Min.x, passthruRect.Max.x), passthruRect.Min.y }, passthruRect.Max };
            }
        case 4: {
            const ImVec2 mid = passthruRect.GetCenter();
            const ImVec2 extent { passthruRect.Max.x - mid.x, passthruRect.Max.y - mid.y };
            const ImVec2 min { (viewIndex % 2 == 0) ? passthruRect.Min.x : mid.x, (viewIndex / 2 == 0) ? passthruRect.Min.y : mid.y };
            return { min, min + extent };
        }
        default:
            std::unreachable();
    }
}

boost::container::static_vector<ImRect, 4> vk_gltf_viewer::Renderer::getViewportRects(const ImRect &passthruRect) const noexcept {
    switch (cameras.size()) {
        case 1:
            return { passthruRect };
        case 2: {
            const float midX = std::midpoint(passthruRect.Min.x, passthruRect.Max.x);
            return {
                { passthruRect.Min, { midX, passthruRect.Max.y } },
                { { midX, passthruRect.Min.y }, passthruRect.Max },
            };
        }
        case 4: {
            const ImVec2 mid = passthruRect.GetCenter();
            return {
                { passthruRect.Min, mid },
                { { mid.x, passthruRect.Min.y }, { passthruRect.Max.x, mid.y } },
                { { passthruRect.Min.x, mid.y }, { mid.x, passthruRect.Max.y } },
                { mid, passthruRect.Max },
            };
        }
        default:
            std::unreachable();
    }
}