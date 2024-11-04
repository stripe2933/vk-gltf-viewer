export module vk_gltf_viewer:gltf.algorithm.traversal;

import std;
export import glm;
export import fastgltf;
import :helpers.fastgltf;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return (__VA_ARGS__)(FWD(xs)...); }

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
            if constexpr (std::convertible_to<std::invoke_result_t<F, std::size_t>, bool>) {
                // If F's return type is bool type, traverse continuation have to be determined by the return value of f.
                if (f(nodeIndex)) {
                    // Continue traversal only if f returns true.
                    const fastgltf::Node &node = asset.nodes[nodeIndex];
                    for (std::size_t childNodeIndex : node.children) {
                        self(childNodeIndex);
                    }
                }
            }
            else {
                f(nodeIndex);
                const fastgltf::Node &node = asset.nodes[nodeIndex];
                for (std::size_t childNodeIndex : node.children) {
                    self(childNodeIndex);
                }
            }
        }(nodeIndex);
    }

    /**
     * Traverse node's descendants with accumulated transforms (i.e. world transform) using preorder traversal.
     * @tparam F Function type that can be executed with node index and <tt>glm::mat4</tt>. If it returns contextually convertible to <tt>bool</tt> type, the return value will be determined as the traversal continuation (<tt>true</tt> -> continue traversal).
     * @param asset fastgltf Asset.
     * @param nodeIndex Node index to start traversal.
     * @param f Function that would be invoked with node index and <tt>glm::mat4</tt>.
     * @param initialNodeWorldTransform World transform matrix of the start node.
     */
    export template <std::invocable<std::size_t, const glm::mat4&> F>
    void traverseNode(const fastgltf::Asset &asset, std::size_t nodeIndex, const F &f, const glm::mat4 &initialNodeWorldTransform) noexcept(std::is_nothrow_invocable_v<F, std::size_t, const glm::mat4&>) {
        [&](this const auto &self, std::size_t nodeIndex, glm::mat4 worldTransform) -> void {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            worldTransform *= visit(LIFT(fastgltf::toMatrix), node.transform);

            if constexpr (std::convertible_to<std::invoke_result_t<F, std::size_t, const glm::mat4&>, bool>) {
                // If F's return type is bool type, traverse continuation have to be determined by the return value of f.
                if (f(nodeIndex, worldTransform)) {
                    // Continue traversal only if f returns true.
                    for (std::size_t childNodeIndex : node.children) {
                        self(childNodeIndex, worldTransform);
                    }
                }
            }
            else {
                f(nodeIndex, worldTransform);
                for (std::size_t childNodeIndex : node.children) {
                    self(childNodeIndex, worldTransform);
                }
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
     * @tparam F Function type that can be executed with node index and <tt>glm::mat4</tt>. If it returns contextually convertible to <tt>bool</tt> type, the return value will be determined as the traversal continuation (<tt>true</tt> -> continue traversal).
     * @param asset fastgltf Asset.
     * @param scene fastgltf Scene. This must be originated from \p asset.
     * @param f Function that would be invoked with node index and <tt>glm::mat4</tt>.
     */
    export template <std::invocable<std::size_t, const glm::mat4&> F>
    void traverseScene(const fastgltf::Asset &asset, const fastgltf::Scene &scene, const F &f) noexcept(std::is_nothrow_invocable_v<F, std::size_t, const glm::mat4&>) {
        for (std::size_t nodeIndex : scene.nodeIndices) {
            traverseNode(asset, nodeIndex, f, glm::mat4 { 1.f });
        }
    }
}