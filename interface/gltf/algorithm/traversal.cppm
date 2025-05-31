export module vk_gltf_viewer.gltf.algorithm.traversal;

import std;
export import fastgltf;

namespace vk_gltf_viewer::gltf::algorithm {
    /**
     * Traverse node's descendants using preorder traversal.
     * @tparam F Function type that can be executed with node index. If it returns contextually convertible to <tt>bool</tt> type, the return value will be determined as the traversal continuation (<tt>true</tt> -> continue traversal).
     * @param asset fastgltf Asset.
     * @param scene Node index to start traversal.
     * @param f Function that would be invoked with node index.
     */
    export template <std::invocable<std::size_t> F>
    void traverseNode(const fastgltf::Asset &asset, std::size_t nodeIndex, const F &f) noexcept(std::is_nothrow_invocable_v<F, std::size_t>) {
        [&](this const auto &self, std::size_t nodeIndex) -> void {
            // If F is predicate, traversal continuation is determined by the return value of f.
            if constexpr (std::predicate<F, std::size_t>) {
                // Stop traversal if f returns false.
                if (!f(nodeIndex)) return;
            }
            else {
                f(nodeIndex);
            }
            
            for (std::size_t childNodeIndex : asset.nodes[nodeIndex].children) {
                self(childNodeIndex);
            }
        }(nodeIndex);
    }

    /**
     * Traverse node's descendants with accumulated transforms (i.e. world transform) using preorder traversal.
     * @tparam F Function type that can be executed with node index and <tt>fastgltf::math::fmat4x4</tt>. If it returns contextually convertible to <tt>bool</tt> type, the return value will be determined as the traversal continuation (<tt>true</tt> -> continue traversal).
     * @param asset fastgltf Asset.
     * @param nodeIndex Node index to start traversal.
     * @param f Function that would be invoked with node index and <tt>fastgltf::math::fmat4x4</tt>.
     * @param initialNodeWorldTransform World transform matrix of the start node.
     */
    export template <std::invocable<std::size_t, const fastgltf::math::fmat4x4&> F>
    void traverseNode(const fastgltf::Asset &asset, std::size_t nodeIndex, const F &f, const fastgltf::math::fmat4x4 &initialNodeWorldTransform) noexcept(std::is_nothrow_invocable_v<F, std::size_t, const fastgltf::math::fmat4x4&>) {
        [&](this const auto &self, std::size_t nodeIndex, const fastgltf::math::fmat4x4 &worldTransform) -> void {
            // If F is predicate, traversal continuation is determined by the return value of f.
            if constexpr (std::predicate<F, std::size_t, const fastgltf::math::fmat4x4&>) {
                // Stop traversal if f returns false.
                if (!f(nodeIndex, worldTransform)) return;
            }
            else {
                f(nodeIndex, worldTransform);
            }
            
            for (std::size_t childNodeIndex : asset.nodes[nodeIndex].children) {
                const fastgltf::math::fmat4x4 childNodeWorldTransform = fastgltf::getTransformMatrix(asset.nodes[childNodeIndex], worldTransform);
                self(childNodeIndex, childNodeWorldTransform);
            }
        }(nodeIndex, initialNodeWorldTransform);
    }

    /**
     * Traverse \p scene using preorder traversal.
     * @tparam F Function type that can be executed with node index. If it returns contextually convertible to <tt>bool</tt> type, the return value will be determined as the traversal continuation (<tt>true</tt> -> continue traversal).
     * @param asset fastgltf Asset.
     * @param scene fastgltf Scene. This must be originated from \p asset.
     * @param f Function that would be invoked with node index.
     */
    export template <std::invocable<std::size_t> F>
    void traverseScene(const fastgltf::Asset &asset, const fastgltf::Scene &scene, const F &f) noexcept(std::is_nothrow_invocable_v<F, std::size_t>) {
        for (std::size_t nodeIndex : scene.nodeIndices) {
            traverseNode(asset, nodeIndex, f);
        }
    }

    /**
     * Traverse \p scene with accumulated transforms (i.e. world transform) using preorder traversal.
     * @tparam F Function type that can be executed with node index and <tt>fastgltf::math::fmat4x4</tt>. If it returns contextually convertible to <tt>bool</tt> type, the return value will be determined as the traversal continuation (<tt>true</tt> -> continue traversal).
     * @param asset fastgltf Asset.
     * @param scene fastgltf Scene. This must be originated from \p asset.
     * @param f Function that would be invoked with node index and <tt>fastgltf::math::fmat4x4</tt>.
     */
    export template <std::invocable<std::size_t, const fastgltf::math::fmat4x4&> F>
    void traverseScene(const fastgltf::Asset &asset, const fastgltf::Scene &scene, const F &f) noexcept(std::is_nothrow_invocable_v<F, std::size_t, const fastgltf::math::fmat4x4&>) {
        for (std::size_t nodeIndex : scene.nodeIndices) {
            traverseNode(asset, nodeIndex, f, fastgltf::getTransformMatrix(asset.nodes[nodeIndex]));
        }
    }
}