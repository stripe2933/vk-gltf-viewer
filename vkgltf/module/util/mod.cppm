export module vkgltf.util;

import std;
export import fastgltf;

namespace vkgltf::utils {
    /**
     * @brief Get the index of the texture coordinate attribute for the given texture info, considering
     * <tt>KHR_texture_transform</tt> extension.
     *
     * If <tt>KHR_texture_transform</tt> extension defines a texture coordinate index, it will be returned.
     * Otherwise, <tt>textureInfo.texCoordIndex</tt> will be returned.
     *
     * @param textureInfo Texture info to get the texture coordinate index from.
     * @return Texture coordinate index.
     */
    export
    [[nodiscard]] std::size_t getTexcoordIndex(const fastgltf::TextureInfo &textureInfo) noexcept;

    /**
     * @brief Get target weight count of \p node, with respecting its mesh target weights existence.
     *
     * glTF 2.0 specification:
     *   A mesh with morph targets MAY also define an optional mesh.weights property that stores the default targets'
     *   weights. These weights MUST be used when node.weights is undefined. When mesh.weights is undefined, the default
     *   targets' weights are zeros.
     *
     * Therefore, when calculating the count of a node's target weights, its mesh target weights MUST be also considered.
     *
     * @param asset Asset that is owning \p node.
     * @param node Node to get the target weight count.
     * @return Target weight count.
     */
    export
    [[nodiscard]] std::size_t getTargetWeightCount(const fastgltf::Asset &asset, const fastgltf::Node &node) noexcept;

    /**
     * @brief Get non-owning target weights of \p node, with respecting its mesh target weights existence.
     *
     * glTF 2.0 specification:
     *   A mesh with morph targets MAY also define an optional mesh.weights property that stores the default targets'
     *   weights. These weights MUST be used when node.weights is undefined. When mesh.weights is undefined, the default
     *   targets' weights are zeros.
     *
     * Therefore, when calculating the count of a node's target weights, its mesh target weights MUST be also considered.
     *
     * @param asset Asset that is owning \p node.
     * @param node Node to get the target weight count.
     * @return <tt>std::span</tt> of \p node 's target weights.
     */
    export
    [[nodiscard]] std::span<const float> getTargetWeights(const fastgltf::Asset &asset, const fastgltf::Node &node) noexcept;

    /**
     * @brief Get transform matrices of \p node instances.
     *
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view. If you provided <tt>fastgltf::Options::LoadExternalBuffers</tt> to the <tt>fastgltf::Parser</tt> while loading the glTF, the parameter can be omitted.
     * @param asset fastgltf asset.
     * @param nodeIndex Node index to get the instance transforms.
     * @param adapter Buffer data adapter.
     * @return A vector of instance transform matrices.
     * @throw std::out_of_range If the node is not instanced.
     * @note This function has effect only if \p asset is loaded with <tt>EXT_mesh_gpu_instancing</tt> extension supporting parser (otherwise, it will return the empty vector).
     */
    export template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    [[nodiscard]] std::vector<fastgltf::math::fmat4x4> getInstanceTransforms(
        const fastgltf::Asset &asset,
        std::size_t nodeIndex,
        const BufferDataAdapter &adapter = {}
    ) {
        const fastgltf::Node &node = asset.nodes[nodeIndex];

        // According to the EXT_mesh_gpu_instancing specification, all attribute accessors in a given node must
        // have the same count. Therefore, we can use the count of the first attribute accessor.
        // std::out_of_range in here means the node is not instanced.
        const std::uint32_t instanceCount = asset.accessors[node.instancingAttributes.at(0).accessorIndex].count;
        std::vector<fastgltf::math::fmat4x4> result(instanceCount);

        if (auto it = node.findInstancingAttribute("TRANSLATION"); it != node.instancingAttributes.end()) {
            const fastgltf::Accessor &accessor = asset.accessors[it->accessorIndex];
            iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, accessor, [&](const fastgltf::math::fvec3 &translation, std::size_t i) {
                result[i] = translate(result[i], translation);
            }, adapter);
        }

        if (auto it = node.findInstancingAttribute("ROTATION"); it != node.instancingAttributes.end()) {
            const fastgltf::Accessor &accessor = asset.accessors[it->accessorIndex];
            iterateAccessorWithIndex<fastgltf::math::fquat>(asset, accessor, [&](fastgltf::math::fquat rotation, std::size_t i) {
                result[i] = rotate(result[i], rotation);
            }, adapter);
        }

        if (auto it = node.findInstancingAttribute("SCALE"); it != node.instancingAttributes.end()) {
            const fastgltf::Accessor &accessor = asset.accessors[it->accessorIndex];
            iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, accessor, [&](const fastgltf::math::fvec3 &scale, std::size_t i) {
                result[i] = fastgltf::math::scale(result[i], scale);
            }, adapter);
        }

        return result;
    }
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

std::size_t vkgltf::utils::getTexcoordIndex(const fastgltf::TextureInfo &textureInfo) noexcept {
    if (const auto &transform = textureInfo.transform) {
        return transform->texCoordIndex.value_or(textureInfo.texCoordIndex);
    }
    return textureInfo.texCoordIndex;
}

std::size_t vkgltf::utils::getTargetWeightCount(const fastgltf::Asset &asset, const fastgltf::Node &node) noexcept {
    std::size_t result = node.weights.size();
    if (node.meshIndex) {
        result = asset.meshes[*node.meshIndex].weights.size();
    }
    return result;
}

std::span<const float> vkgltf::utils::getTargetWeights(const fastgltf::Asset &asset, const fastgltf::Node &node) noexcept {
    std::span result = node.weights;
    if (node.meshIndex) {
        result = asset.meshes[*node.meshIndex].weights;
    }
    return result;
}
