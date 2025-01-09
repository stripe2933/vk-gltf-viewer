export module vk_gltf_viewer:gltf.algorithm.misc;

import std;
export import fastgltf;
import :helpers.ranges;

namespace vk_gltf_viewer::gltf::algorithm {
    /**
     * @brief Get the material index of the given nodes (specified by their indices in \p asset), if they have at most 1 material.
     * @param asset glTF asset.
     * @param nodeIndices Indices of the nodes to check.
     * @return The material index if the nodes have at most 1 material, otherwise <tt>std::nullopt</tt>.
     */
    export
    [[nodiscard]] std::optional<std::size_t> getUniqueMaterialIndex(const fastgltf::Asset &asset, std::ranges::input_range auto &&nodeIndices) noexcept {
        std::optional<std::size_t> uniqueMaterialIndex = std::nullopt;
        for (std::size_t nodeIndex : nodeIndices) {
            const auto &meshIndex = asset.nodes[nodeIndex].meshIndex;
            if (!meshIndex) continue;

            for (const fastgltf::Primitive &primitive : asset.meshes[*meshIndex].primitives) {
                if (primitive.materialIndex) {
                    if (!uniqueMaterialIndex) {
                        uniqueMaterialIndex.emplace(*primitive.materialIndex);
                    }
                    else if (*uniqueMaterialIndex != *primitive.materialIndex) {
                        // The input nodes contain at least 2 materials.
                        return std::nullopt;
                    }
                }
            }
        }
        return uniqueMaterialIndex;
    }

} // namespace vk_gltf_viewer::gltf::algorithm