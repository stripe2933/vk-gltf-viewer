export module vk_gltf_viewer.global;

export import vk_gltf_viewer.control.Camera;
export import vk_gltf_viewer.helpers.full_optional;

namespace vk_gltf_viewer::global {
    /**
     * When a node is selected from the renderer, the selected node needed to be visible in the "Scene Hierarchy" window.
     * This is done by calling <tt>ImGui::ScrollToItem()</tt> after set the tree node. However, it should be done for only
     * once. Therefore, when scrolling is needed, this variable is being <tt>true</tt>, and who is ought to call
     * <tt>ImGui::ScrollToItem()</tt> have to set this to <tt>false</tt>.
     */
    export bool shouldNodeInSceneHierarchyScrolledToBeVisible = false;

    export struct Bloom {
        enum class Mode {
            PerMaterial,
            PerFragment,
        };

        Mode mode;
        float intensity;
    };

    export full_optional<Bloom> bloom { unset, Bloom::Mode::PerMaterial, 0.04f };

    export enum class FrustumCullingMode {
        Off,
        On,
        OnWithInstancing,
    };

    export FrustumCullingMode frustumCullingMode = FrustumCullingMode::OnWithInstancing;

    export control::Camera camera {
        glm::vec3 { 0.f, 0.f, 5.f }, normalize(glm::vec3 { 0.f, 0.f, -1.f }), glm::vec3 { 0.f, 1.f, 0.f },
        glm::radians(45.f), 1.f /* will be determined by passthru rect dimension */, 1e-2f, 10.f,
        5.f,
    };
}