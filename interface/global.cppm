export module vk_gltf_viewer:global;

export import :helpers.full_optional;

namespace vk_gltf_viewer::global {
    /**
     * When a node is selected from the renderer, the selected node needed to be visible in the "Scene Hierarchy" window.
     * This is done by calling <tt>ImGui::ScrollToItem()</tt> after set the tree node. However, it should be done for only
     * once. Therefore, when scrolling is needed, this variable is being <tt>true</tt>, and who is ought to call
     * <tt>ImGui::ScrollToItem()</tt> have to set this to <tt>false</tt>.
     */
    export bool shouldNodeInSceneHierarchyScrolledToBeVisible = false;

    export full_optional<float> bloomIntensity = 0.04f;
}