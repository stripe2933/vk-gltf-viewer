export module vk_gltf_viewer:helpers.fastgltf;

import std;
export import cstring_view;
export import fastgltf;
import :helpers.optional;
import :helpers.type_map;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define DEFINE_FORMATTER(Type) \
    export template <> \
    struct std::formatter<Type> : formatter<string_view> { \
        auto format(Type v, auto &ctx) const { \
            return formatter<string_view>::format(to_string(v), ctx); \
        } \
    }

namespace fastgltf {
    export template <typename T>
    [[nodiscard]] std::optional<T> to_optional(OptionalWithFlagValue<T> v) noexcept {
        return value_if(v.has_value(), [&]() { return *v; });
    }

    export
    [[nodiscard]] auto to_string(PrimitiveType value) noexcept -> cpp_util::cstring_view {
        switch (value) {
            case PrimitiveType::Points: return "Points";
            case PrimitiveType::Lines: return "Lines";
            case PrimitiveType::LineLoop: return "LineLoop";
            case PrimitiveType::LineStrip: return "LineStrip";
            case PrimitiveType::Triangles: return "Triangles";
            case PrimitiveType::TriangleStrip: return "TriangleStrip";
            case PrimitiveType::TriangleFan: return "TriangleFan";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(AccessorType value) noexcept -> cpp_util::cstring_view {
        switch (value) {
            case AccessorType::Invalid: return "Invalid";
            case AccessorType::Scalar: return "Scalar";
            case AccessorType::Vec2: return "Vec2";
            case AccessorType::Vec3: return "Vec3";
            case AccessorType::Vec4: return "Vec4";
            case AccessorType::Mat2: return "Mat2";
            case AccessorType::Mat3: return "Mat3";
            case AccessorType::Mat4: return "Mat4";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(ComponentType value) noexcept -> cpp_util::cstring_view {
        switch (value) {
            case ComponentType::Byte: return "Byte";
            case ComponentType::UnsignedByte: return "UnsignedByte";
            case ComponentType::Short: return "Short";
            case ComponentType::UnsignedShort: return "UnsignedShort";
            case ComponentType::UnsignedInt: return "UnsignedInt";
            case ComponentType::Float: return "Float";
            case ComponentType::Invalid: return "Invalid";
            case ComponentType::Int: return "Int";
            case ComponentType::Double: return "Double";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(BufferTarget target) noexcept -> cpp_util::cstring_view {
        switch (target) {
            case BufferTarget::ArrayBuffer: return "ArrayBuffer";
            case BufferTarget::ElementArrayBuffer: return "ElementArrayBuffer";
            default: return "-";
        }
    }

    export
    [[nodiscard]] auto to_string(MimeType mime) noexcept -> cpp_util::cstring_view {
        switch (mime) {
            case MimeType::None: return "-";
            case MimeType::JPEG: return "image/jpeg";
            case MimeType::PNG: return "image/png";
            case MimeType::KTX2: return "image/ktx2";
            case MimeType::GltfBuffer: return "model/gltf-buffer";
            case MimeType::OctetStream: return "application/octet-stream";
            case MimeType::DDS: return "image/vnd-ms.dds";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(AlphaMode alphaMode) noexcept -> cpp_util::cstring_view {
        switch (alphaMode) {
            case AlphaMode::Opaque: return "Opaque";
            case AlphaMode::Mask: return "Mask";
            case AlphaMode::Blend: return "Blend";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(Filter filter) noexcept -> cpp_util::cstring_view {
        switch (filter) {
            case Filter::Linear: return "Linear";
            case Filter::Nearest: return "Nearest";
            case Filter::LinearMipMapLinear: return "LinearMipMapLinear";
            case Filter::LinearMipMapNearest: return "LinearMipMapNearest";
            case Filter::NearestMipMapLinear: return "NearestMipMapLinear";
            case Filter::NearestMipMapNearest: return "NearestMipMapNearest";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(Wrap wrap) noexcept -> cpp_util::cstring_view {
        switch (wrap) {
            case Wrap::Repeat: return "Repeat";
            case Wrap::ClampToEdge: return "ClampToEdge";
            case Wrap::MirroredRepeat: return "MirroredRepeat";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] cpp_util::cstring_view to_string(AnimationPath path) noexcept {
        switch (path) {
            case AnimationPath::Translation: return "translation";
            case AnimationPath::Rotation: return "rotation";
            case AnimationPath::Scale: return "scale";
            case AnimationPath::Weights: return "weights";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] cpp_util::cstring_view to_string(AnimationInterpolation interpolation) noexcept {
        switch (interpolation) {
            case AnimationInterpolation::Linear: return "LINEAR";
            case AnimationInterpolation::Step: return "STEP";
            case AnimationInterpolation::CubicSpline: return "CUBICSPLINE";
        }
        std::unreachable();
    }

    /**
     * @brief Convert TRS to 4x4 matrix.
     * @param trs TRS to convert.
     * @param matrix matrix to be transformed. Default: identity matrix.
     * @return 4x4 matrix.
     */
    export
    [[nodiscard]] math::fmat4x4 toMatrix(const TRS &trs, const math::fmat4x4 &matrix = math::fmat4x4 { 1.f }) noexcept {
        return scale(rotate(translate(matrix, trs.translation), trs.rotation), trs.scale);
    }

    /**
     * Get buffer byte region of \p accessor.
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view. If you provided <tt>fastgltf::Options::LoadExternalBuffers</tt> to the <tt>fastgltf::Parser</tt> while loading the glTF, the parameter can be omitted.
     * @param asset fastgltf Asset.
     * @param accessor Accessor to get the byte region.
     * @param adapter Buffer data adapter.
     * @return Span of bytes.
     * @throw std::runtime_error If the accessor doesn't have buffer view.
     */
    export template <typename BufferDataAdapter = DefaultBufferDataAdapter>
    [[nodiscard]] std::span<const std::byte> getByteRegion(const Asset &asset, const Accessor &accessor, const BufferDataAdapter &adapter = {}) {
        if (!accessor.bufferViewIndex) throw std::runtime_error { "No buffer view in accessor." };

        const BufferView &bufferView = asset.bufferViews[*accessor.bufferViewIndex];
        const std::size_t byteStride = bufferView.byteStride.value_or(getElementByteSize(accessor.type, accessor.componentType));
        return adapter(asset, *accessor.bufferViewIndex).subspan(accessor.byteOffset, byteStride * accessor.count);
    }

    /**
     * @brief Get the instance transform for given node.
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view. If you provided <tt>fastgltf::Options::LoadExternalBuffers</tt> to the <tt>fastgltf::Parser</tt> while loading the glTF, the parameter can be omitted.
     * @param asset fastgltf asset
     * @param nodeIndex Node index to get the instance transforms.
     * @param instanceIndex Instance index to get the transform.
     * @param adapter Buffer data adapter.
     * @return Instance transform matrix.
     * @throw std::invalid_argument If the node is not instanced.
     */
    export template <typename BufferDataAdapter = DefaultBufferDataAdapter>
    [[nodiscard]] math::fmat4x4 getInstanceTransform(const Asset &asset, std::size_t nodeIndex, std::size_t instanceIndex, const BufferDataAdapter &adapter = {}) {
        const Node &node = asset.nodes[nodeIndex];
        if (node.instancingAttributes.empty()) {
            throw std::invalid_argument { "Node is not instanced" };
        }

        math::fmat4x4 result;
        if (auto it = node.findInstancingAttribute("TRANSLATION"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->accessorIndex];
            const math::fvec3 translation = getAccessorElement<math::fvec3>(asset, accessor, instanceIndex, adapter);
            result = translate(result, translation);
        }
        if (auto it = node.findInstancingAttribute("ROTATION"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->accessorIndex];
            math::fquat rotation = getAccessorElement<math::fquat>(asset, accessor, instanceIndex, adapter);
            result = rotate(result, rotation);
        }
        if (auto it = node.findInstancingAttribute("SCALE"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->accessorIndex];
            const math::fvec3 scale = getAccessorElement<math::fvec3>(asset, accessor, instanceIndex, adapter);
            result = math::scale(result, scale);
        }

        return result;
    }

    /**
     * @brief Get transform matrices of \p node instances.
     *
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view. If you provided <tt>fastgltf::Options::LoadExternalBuffers</tt> to the <tt>fastgltf::Parser</tt> while loading the glTF, the parameter can be omitted.
     * @param asset fastgltf asset.
     * @param nodeIndex Node index to get the instance transforms.
     * @param adapter Buffer data adapter.
     * @return A vector of instance transform matrices.
     * @throw std::invalid_argument If the node is not instanced.
     * @note This function has effect only if \p asset is loaded with EXT_mesh_gpu_instancing extension supporting parser (otherwise, it will return the empty vector).
     */
    export template <typename BufferDataAdapter = DefaultBufferDataAdapter>
    [[nodiscard]] std::vector<math::fmat4x4> getInstanceTransforms(const Asset &asset, std::size_t nodeIndex, const BufferDataAdapter &adapter = {}) {
        const Node &node = asset.nodes[nodeIndex];
        if (node.instancingAttributes.empty()) {
            throw std::invalid_argument { "Node is not instanced" };
        }

        // According to the EXT_mesh_gpu_instancing specification, all attribute accessors in a given node must
        // have the same count. Therefore, we can use the count of the first attribute accessor.
        const std::uint32_t instanceCount = asset.accessors[node.instancingAttributes[0].accessorIndex].count;
        std::vector<math::fmat4x4> result(instanceCount);

        if (auto it = node.findInstancingAttribute("TRANSLATION"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->accessorIndex];
            iterateAccessorWithIndex<math::fvec3>(asset, accessor, [&](const math::fvec3 &translation, std::size_t i) {
                result[i] = translate(result[i], translation);
            }, adapter);
        }

        if (auto it = node.findInstancingAttribute("ROTATION"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->accessorIndex];
            iterateAccessorWithIndex<math::fquat>(asset, accessor, [&](math::fquat rotation, std::size_t i) {
                result[i] = rotate(result[i], rotation);
            }, adapter);
        }

        if (auto it = node.findInstancingAttribute("SCALE"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->accessorIndex];
            iterateAccessorWithIndex<math::fvec3>(asset, accessor, [&](const math::fvec3 &scale, std::size_t i) {
                result[i] = math::scale(result[i], scale);
            }, adapter);
        }

        return result;
    }

    /**
     * @brief Unwrap the value of the expected, or <tt>fastgltf::Error</tt> if it is an error.
     * @tparam T Type of the expected value. This have to be move constructible.
     * @param expected fastgltf Expected.
     * @return The value of the expected if it is not an error.
     * @throw fastgltf::Error If the expected is an error.
     */
    export template <std::move_constructible T>
    [[nodiscard]] T get_checked(Expected<T> expected) {
        if (expected) {
            return std::move(expected.get());
        }
        throw expected.error();
    }

    /**
     * @brief Get texture coordinate index from \p textureInfo, respecting KHR_texture_transform extension.
     *
     * Fetching texture coordinate index should not rely on \p textureInfo.texCoordIndex directly, because
     * KHR_texture_transform extension overrides the index. Using this function is encouraged.
     *
     * @param textureInfo Texture info to get the texture coordinate index.
     * @return Texture coordinate index.
     */
    export
    [[nodiscard]] std::size_t getTexcoordIndex(const TextureInfo &textureInfo) noexcept {
        if (textureInfo.transform && textureInfo.transform->texCoordIndex) {
            return *textureInfo.transform->texCoordIndex;
        }
        return textureInfo.texCoordIndex;
    }

    /**
     * Get image index from \p texture with preference of GPU compressed texture.
     *
     * You should use this function to get the image index from a texture, rather than directly access such like
     * <tt>texture.imageIndex</tt> or <tt>texture.basisuImageIndex</tt>.
     *
     * @param texture Texture to get the index.
     * @return Image index.
     */
    export
    [[nodiscard]] std::size_t getPreferredImageIndex(const Texture &texture) {
        return to_optional(texture.basisuImageIndex) // Prefer BasisU compressed image if exists.
            .or_else([&]() { return to_optional(texture.imageIndex); }) // Otherwise, use regular image.
            .value();
    }

    /**
     * @brief Create a byte vector that contains the tightly packed accessor data.
     *
     * Following accessor types and accessor component types are supported:
     * Accessor types: Scalar, Vec2, Vec3, Vec4
     * Component types: Byte, UnsignedByte, Short, UnsignedShort, Int, UnsignedInt, Float
     *
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view.
     * @param accessor Accessor to get the data.
     * @param asset Asset that is owning \p accessor.
     * @param adapter Buffer data adapter.
     * @return Byte vector that contains the accessor data.
     * @throw std::out_of_range If <tt>accessor.type</tt> or <tt>accessor.componentType</tt> is not supported.
     */
    export template <typename BufferDataAdapter = DefaultBufferDataAdapter>
    [[nodiscard]] std::vector<std::byte> getAccessorByteData(const Accessor &accessor, const Asset &asset, const BufferDataAdapter &adapter = {}) {
        std::vector<std::byte> data(getElementByteSize(accessor.type, accessor.componentType) * accessor.count);

        constexpr type_map accessorTypeMap {
            make_type_map_entry<std::integral_constant<int, 1>>(AccessorType::Scalar),
            make_type_map_entry<std::integral_constant<int, 2>>(AccessorType::Vec2),
            make_type_map_entry<std::integral_constant<int, 3>>(AccessorType::Vec3),
            make_type_map_entry<std::integral_constant<int, 4>>(AccessorType::Vec4),
        };
        constexpr type_map componentTypeMap {
            make_type_map_entry<std::int8_t>(ComponentType::Byte),
            make_type_map_entry<std::uint8_t>(ComponentType::UnsignedByte),
            make_type_map_entry<std::int16_t>(ComponentType::Short),
            make_type_map_entry<std::uint16_t>(ComponentType::UnsignedShort),
            make_type_map_entry<std::int32_t>(ComponentType::Int),
            make_type_map_entry<std::uint32_t>(ComponentType::UnsignedInt),
            make_type_map_entry<float>(ComponentType::Float),
        };
        std::visit([&]<int ComponentCount, typename ComponentType>(std::type_identity<std::integral_constant<int, ComponentCount>>, std::type_identity<ComponentType>) {
            if constexpr (ComponentCount == 1) {
                copyFromAccessor<ComponentType>(asset, accessor, data.data(), adapter);
            }
            else {
                copyFromAccessor<math::vec<ComponentType, ComponentCount>>(asset, accessor, data.data(), adapter);
            }
        }, accessorTypeMap.get_variant(accessor.type), componentTypeMap.get_variant(accessor.componentType));

        return data;
    }

    /**
     * @brief Get non-owning target weights of \p node, with respecting its mesh target weights existence.
     *
     * glTF spec:
     *   A mesh with morph targets MAY also define an optional mesh.weights property that stores the default targets'
     *   weights. These weights MUST be used when node.weights is undefined. When mesh.weights is undefined, the default
     *   targets' weights are zeros.
     *
     * Therefore, when calculating the count of a node's target weights, its mesh target weights MUST be also considered.
     *
     * @param node Node to get the target weight count.
     * @param asset Asset that is owning \p node.
     * @return <tt>std::span</tt> of \p node 's target weights.
     */
    export
    [[nodiscard]] std::span<float> getTargetWeights(Node &node, Asset &asset) noexcept {
        std::span weights = node.weights;
        if (node.meshIndex) {
            weights = asset.meshes[*node.meshIndex].weights;
        }
        return weights;
    }

    /**
     * @copydoc getTargetWeights
     */
    export
    [[nodiscard]] std::span<const float> getTargetWeights(const Node &node, const Asset &asset) noexcept {
        std::span weights = node.weights;
        if (node.meshIndex) {
            weights = asset.meshes[*node.meshIndex].weights;
        }
        return weights;
    }

    /**
     * @brief Get target weight count of \p node, with respecting its mesh target weights existence.
     *
     * glTF spec:
     *   A mesh with morph targets MAY also define an optional mesh.weights property that stores the default targets'
     *   weights. These weights MUST be used when node.weights is undefined. When mesh.weights is undefined, the default
     *   targets' weights are zeros.
     *
     * Therefore, when calculating the count of a node's target weights, its mesh target weights MUST be also considered.
     *
     * @param node Node to get the target weight count.
     * @param asset Asset that is owning \p node.
     * @return Target weight count.
     */
    export
    [[nodiscard]] std::size_t getTargetWeightCount(const Node &node, const Asset &asset) noexcept {
        std::size_t count = node.weights.size();
        if (node.meshIndex) {
            count = asset.meshes[*node.meshIndex].weights.size();
        }
        return count;
    }

namespace math {
    /**
     * @brief Convert matrix of type \tp U to matrix of type \tp T.
     * @tparam T The destination matrix type.
     * @tparam U The source matrix type.
     * @tparam N The number of columns.
     * @tparam M The number of rows.
     * @param m The source matrix.
     * @return The converted matrix of type \tp T.
     */
    export template <typename T, typename U, std::size_t N, std::size_t M>
    [[nodiscard]] mat<T, N, M> cast(const mat<U, N, M> &m) noexcept {
        return INDEX_SEQ(Is, M, {
            return mat<T, N, M> { vec<T, N> { m[Is] }... };
        });
    }

    /**
     * @brief Get component-wise minimum of two vectors.
     * @tparam T Vector component type.
     * @tparam N Number of vector components.
     * @param lhs
     * @param rhs
     * @return Component-wise minimum of two vectors.
     */
    export template <typename T, std::size_t N>
    [[nodiscard]] vec<T, N> cwiseMin(vec<T, N> lhs, const vec<T, N> &rhs) noexcept {
        INDEX_SEQ(Is, N, {
            ((lhs.data()[Is] = std::min(lhs.data()[Is], rhs.data()[Is])), ...);
        });
        return lhs;
    }

    /**
     * @brief Get component-wise maximum of two vectors.
     * @tparam T Vector component type.
     * @tparam N Number of vector components.
     * @param lhs
     * @param rhs
     * @return Component-wise maximum of two vectors.
     */
    export template <typename T, std::size_t N>
    [[nodiscard]] vec<T, N> cwiseMax(vec<T, N> lhs, const vec<T, N> &rhs) noexcept {
        INDEX_SEQ(Is, N, {
            ((lhs.data()[Is] = std::max(lhs.data()[Is], rhs.data()[Is])), ...);
        });
        return lhs;
    }
}
}

DEFINE_FORMATTER(fastgltf::PrimitiveType);
DEFINE_FORMATTER(fastgltf::AccessorType);
DEFINE_FORMATTER(fastgltf::ComponentType);
DEFINE_FORMATTER(fastgltf::BufferTarget);
DEFINE_FORMATTER(fastgltf::MimeType);
DEFINE_FORMATTER(fastgltf::AlphaMode);
DEFINE_FORMATTER(fastgltf::Filter);
DEFINE_FORMATTER(fastgltf::Wrap);
DEFINE_FORMATTER(fastgltf::AnimationPath);
DEFINE_FORMATTER(fastgltf::AnimationInterpolation);