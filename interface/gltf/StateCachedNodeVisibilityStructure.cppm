export module vk_gltf_viewer.gltf.StateCachedNodeVisibilityStructure;

import std;
export import fastgltf;

import vk_gltf_viewer.gltf.algorithm.traversal;
import vk_gltf_viewer.gltf.data_structure.SceneInverseHierarchy;
import vk_gltf_viewer.helpers.ranges;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief A data structure owning the node visibility and having the information about a node and its descendants' states.
     *
     * Node visibility is stored using <tt>std::vector<bool></tt>, whose element at index <tt>i</tt> represents the
     * visibility of the node. You can retrieve the whole vector as an immutable reference by calling <tt>getVisibilities()</tt>,
     * and adjust a single node's visibility by calling <tt>setVisibility(std::size_t nodeIndex, bool value)</tt> or
     * <tt>flipVisibility(std::size_t nodeIndex)</tt>.
     *
     * Also, you can query whether the node and its descendants are visible, invisible, intermediate, or indeterminate
     * in a constant time by calling <tt>>getState(std::size_t nodeIndex)</tt>. For detail of the term "visible", "invisible",
     * "intermediate", and "indeterminate", see <tt>State</tt> enum class. These states are calculated when modifying
     * visibility of nodes, and cached in the structure.
     */
    export class StateCachedNodeVisibilityStructure {
    public:
        enum class State : std::uint8_t {
            AllVisible = 0,    /// Every node that has a mesh is visible among the current and its descendants.
            AllInvisible = 1,  /// Every node that has a mesh is invisible among the current and its descendants.
            Intermediate = 2,  /// Every node that has a mesh is mixed state (some are visible, some are invisible)
                               /// among the current and its descendants.
            Indeterminate = 3, /// None of the current and its descendant nodes has a mesh.
        };

        /**
         * @param asset glTF asset.
         * @param scene Scene that determines the hierarchy.
         * @param sceneInverseHierarchy Cached structure for retrieving the parent node index of a node. If not given, new instance will be created.
         */
        StateCachedNodeVisibilityStructure(
            const fastgltf::Asset &asset,
            const fastgltf::Scene &scene,
            std::shared_ptr<const ds::SceneInverseHierarchy> sceneInverseHierarchy = nullptr
        ) noexcept;

        /**
         * @param scene Scene that determines the hierarchy.
         * @param sceneInverseHierarchy Cached structure for retrieving the parent node index of a node. If not given, new instance will be created.
         */
        void setScene(const fastgltf::Scene &scene, std::shared_ptr<const ds::SceneInverseHierarchy> sceneInverseHierarchy = nullptr) noexcept;

        [[nodiscard]] const std::vector<bool> &getVisibilities() const noexcept;

        [[nodiscard]] bool getVisibility(std::size_t nodeIndex) const noexcept;

        /**
         * @brief Set visibility of the node, and propagate the state to its ancestor.
         * @param nodeIndex Node index.
         * @param value New visibility value.
         * @throw std::invalid_argument If the node does not have a mesh.
         */
        void setVisibility(std::size_t nodeIndex, bool value);

        /**
         * @brief Flip the visibility of the node, and propagate the state to its ancestor.
         * @param nodeIndex Node index.
         * @throw std::invalid_argument If the node does not have a mesh.
         * @note It internally calls <tt>setVisibility(std::size_t nodeIndex, bool value)</tt>.
         */
        void flipVisibility(std::size_t nodeIndex);

        [[nodiscard]] State getState(std::size_t nodeIndex) const noexcept;

    private:
        std::reference_wrapper<const fastgltf::Asset> asset;
        std::shared_ptr<const ds::SceneInverseHierarchy> sceneInverseHierarchy;
        std::vector<bool> visibilities;
        std::vector<State> states;

        /**
         * @brief Update the node state by considering its visibility and children's state.
         * @param nodeIndex Node index.
         */
        void updateState(std::size_t nodeIndex) noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::gltf::StateCachedNodeVisibilityStructure::StateCachedNodeVisibilityStructure(
    const fastgltf::Asset &asset,
    const fastgltf::Scene &scene,
    std::shared_ptr<const ds::SceneInverseHierarchy> sceneInverseHierarchy
) noexcept
    : asset { asset }
    , visibilities { std::vector<bool>(asset.nodes.size()) }
    , states { std::vector<State>(asset.nodes.size()) }{
    setScene(scene, std::move(sceneInverseHierarchy));
}

void vk_gltf_viewer::gltf::StateCachedNodeVisibilityStructure::setScene(
    const fastgltf::Scene &scene,
    std::shared_ptr<const ds::SceneInverseHierarchy> sceneInverseHierarchy
) noexcept {
    if (sceneInverseHierarchy) {
        this->sceneInverseHierarchy = std::move(sceneInverseHierarchy);
    }
    else {
        this->sceneInverseHierarchy = std::make_shared<const ds::SceneInverseHierarchy>(asset, scene);
    }

    std::ranges::fill(visibilities, false);
    algorithm::traverseScene(asset, scene, [&](std::size_t nodeIndex) noexcept {
        visibilities[nodeIndex] = true;
    });

    const auto dfs = [this](this const auto &self, std::size_t nodeIndex) -> void {
        // First update all children nodes.
        for (std::size_t childNodeIndex : asset.get().nodes[nodeIndex].children) {
            self(childNodeIndex);
        }

        // And update the current node based on its children.
        updateState(nodeIndex);
    };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        dfs(nodeIndex);
    }
}

const std::vector<bool> &vk_gltf_viewer::gltf::StateCachedNodeVisibilityStructure::getVisibilities() const noexcept {
    return visibilities;
}

bool vk_gltf_viewer::gltf::StateCachedNodeVisibilityStructure::getVisibility(std::size_t nodeIndex) const noexcept {
    return visibilities[nodeIndex];
}

void vk_gltf_viewer::gltf::StateCachedNodeVisibilityStructure::setVisibility(std::size_t nodeIndex, bool value) {
    if (!asset.get().nodes[nodeIndex].meshIndex) {
        throw std::invalid_argument { "Node visibility can only be set for mesh nodes." };
    }

    visibilities[nodeIndex] = value;

    updateState(nodeIndex);
    for (std::optional<std::size_t> parentNodeIndex; (parentNodeIndex = sceneInverseHierarchy->parentNodeIndices[nodeIndex]);) {
        updateState(nodeIndex = *parentNodeIndex);
    }
}

void vk_gltf_viewer::gltf::StateCachedNodeVisibilityStructure::flipVisibility(std::size_t nodeIndex) {
    setVisibility(nodeIndex, !visibilities[nodeIndex]);
}

vk_gltf_viewer::gltf::StateCachedNodeVisibilityStructure::State vk_gltf_viewer::gltf::StateCachedNodeVisibilityStructure::getState(std::size_t nodeIndex) const noexcept {
    return states[nodeIndex];
}

void vk_gltf_viewer::gltf::StateCachedNodeVisibilityStructure::updateState(std::size_t nodeIndex) noexcept {
    State &result = states[nodeIndex];

    const fastgltf::Node &node = asset.get().nodes[nodeIndex];
    if (node.meshIndex) {
        result = visibilities[nodeIndex] ? State::AllVisible : State::AllInvisible;
    }
    else {
        result = State::Indeterminate;
    }

    for (std::size_t childNodeIndex : node.children) {
        const State childState = states[childNodeIndex];

        if (childState == State::Indeterminate) {
            // Child node is meshless, doesn't affect to the state.
        }
        else if (childState == State::AllVisible &&
            ranges::one_of(result, { State::AllVisible, State::Indeterminate })) {
            result = State::AllVisible;
        }
        else if (childState == State::AllInvisible &&
            ranges::one_of(result, { State::AllInvisible, State::Indeterminate })) {
            result = State::AllInvisible;
        }
        else {
            // Child node is either in intermediate state, or visibility is different from the current.
            result = State::Intermediate;
        }
    }
}