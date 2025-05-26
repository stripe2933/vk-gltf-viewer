export module vk_gltf_viewer:global;

import std;
export import glm;
export import vk_gltf_viewer.helpers.full_variant;
export import :helpers.full_optional;

namespace vk_gltf_viewer::global {
    /**
     * When a node is selected from the renderer, the selected node needed to be visible in the "Scene Hierarchy" window.
     * This is done by calling <tt>ImGui::ScrollToItem()</tt> after set the tree node. However, it should be done for only
     * once. Therefore, when scrolling is needed, this variable is being <tt>true</tt>, and who is ought to call
     * <tt>ImGui::ScrollToItem()</tt> have to set this to <tt>false</tt>.
     */
    export bool shouldNodeInSceneHierarchyScrolledToBeVisible = false;

    export full_optional<float> bloomIntensity { unset, 0.04f };

    struct BoundingBox { glm::vec4 color; float enlarge; };
    struct BoundingSphere { glm::vec4 color; float enlarge; };

    /**
     * Debug bounding box properties of the node primitive.
     * If <tt>std::nullopt</tt>, the bounding box will not be rendered.
     */
    export full_variant<std::monostate, BoundingBox, BoundingSphere> nodeBoundingVolume {
        0,
        std::monostate{},
        BoundingBox { glm::vec4 { 0.2f, 0.5f, 1.f, 0.2f }, 1.f },
        BoundingSphere { glm::vec4 { 0.2f, 0.5f, 1.f, 0.2f }, 1.f },
    };
}