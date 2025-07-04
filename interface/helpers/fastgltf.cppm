module;

#include <cassert>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.helpers.fastgltf;

import std;
export import cstring_view;
export import fastgltf;

import vk_gltf_viewer.helpers.optional;
import vk_gltf_viewer.helpers.type_map;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
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
    [[nodiscard]] cpp_util::cstring_view to_string(PrimitiveType value) noexcept;

    export
    [[nodiscard]] cpp_util::cstring_view to_string(AccessorType value) noexcept;

    export
    [[nodiscard]] cpp_util::cstring_view to_string(ComponentType value) noexcept;

    export
    [[nodiscard]] cpp_util::cstring_view to_string(BufferTarget target) noexcept;

    export
    [[nodiscard]] cpp_util::cstring_view to_string(MimeType mime) noexcept;

    export
    [[nodiscard]] cpp_util::cstring_view to_string(AlphaMode alphaMode) noexcept;

    export
    [[nodiscard]] cpp_util::cstring_view to_string(Filter filter) noexcept;

    export
    [[nodiscard]] cpp_util::cstring_view to_string(Wrap wrap) noexcept;

    export
    [[nodiscard]] cpp_util::cstring_view to_string(AnimationPath path) noexcept;

    export
    [[nodiscard]] cpp_util::cstring_view to_string(AnimationInterpolation interpolation) noexcept;

    /**
     * @brief Convert TRS to 4x4 matrix.
     * @param trs TRS to convert.
     * @param matrix matrix to be transformed. Default: identity matrix.
     * @return 4x4 matrix.
     */
    export
    [[nodiscard]] math::fmat4x4 toMatrix(const TRS &trs, const math::fmat4x4 &matrix = math::fmat4x4 { 1.f }) noexcept;

    /**
     * @brief Invoke \p f with non-const reference of \p node's 4x4 local transform matrix.
     *
     * If <tt>node.transform</tt> is:
     * - <tt>fastgltf::math::fmat4x4</tt>, \p f is directly invoked with the matrix.
     * - <tt>fastgltf::TRS</tt>, it is first converted to 4x4 matrix, \p f invoked, then restored to <tt>fastgltf::TRS</tt> when changed.
     *
     * @param node Node whose transform will be used.
     * @param f Function to invoke.
     */
    export template <std::invocable<math::fmat4x4&> F>
    void updateTransform(Node &node, F &&f) noexcept(std::is_nothrow_invocable_v<F, math::fmat4x4&>) {
        visit(visitor {
            [&](TRS &trs) {
                math::fmat4x4 matrix = toMatrix(trs);
                std::invoke(FWD(f), matrix);
                decomposeTransformMatrix(matrix, trs.scale, trs.rotation, trs.translation);
            },
            [&](math::fmat4x4 &matrix) {
                std::invoke(FWD(f), matrix);
            },
        }, node.transform);
    }

    /**
     * Get buffer byte region of \p accessor.
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view. If you provided <tt>fastgltf::Options::LoadExternalBuffers</tt> to the <tt>fastgltf::Parser</tt> while loading the glTF, the parameter can be omitted.
     * @param asset fastgltf Asset.
     * @param accessor Accessor to get the byte region.
     * @param adapter Buffer data adapter.
     * @return Span of bytes.
     * @throw std::bad_optional_access If the accessor doesn't have buffer view.
     */
    export template <typename BufferDataAdapter = DefaultBufferDataAdapter>
    [[nodiscard]] std::span<const std::byte> getByteRegion(const Asset &asset, const Accessor &accessor, const BufferDataAdapter &adapter = {}) {
        const BufferView &bufferView = asset.bufferViews[accessor.bufferViewIndex.value()];
        const std::size_t elementByteSize = getElementByteSize(accessor.type, accessor.componentType);
        const std::size_t byteStride = bufferView.byteStride.value_or(elementByteSize);
        return adapter(asset, *accessor.bufferViewIndex).subspan(accessor.byteOffset, byteStride * (accessor.count - 1) + elementByteSize);
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
     * @throw std::out_of_range If the node is not instanced.
     * @note This function has effect only if \p asset is loaded with EXT_mesh_gpu_instancing extension supporting parser (otherwise, it will return the empty vector).
     */
    export template <typename BufferDataAdapter = DefaultBufferDataAdapter>
    [[nodiscard]] std::vector<math::fmat4x4> getInstanceTransforms(const Asset &asset, std::size_t nodeIndex, const BufferDataAdapter &adapter = {}) {
        const Node &node = asset.nodes[nodeIndex];

        // According to the EXT_mesh_gpu_instancing specification, all attribute accessors in a given node must
        // have the same count. Therefore, we can use the count of the first attribute accessor.
        // std::out_of_range in here means the node is not instanced.
        const std::uint32_t instanceCount = asset.accessors[node.instancingAttributes.at(0).accessorIndex].count;
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
    [[nodiscard]] std::size_t getTexcoordIndex(const TextureInfo &textureInfo) noexcept;

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
    [[nodiscard]] std::size_t getPreferredImageIndex(const Texture &texture);

    /**
     * @brief Create a byte vector that contains the 4-byte aligned accessor data.
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
    [[nodiscard]] std::vector<std::byte> getVertexAttributeAccessorByteData(const Accessor &accessor, const Asset &asset, const BufferDataAdapter &adapter = {}) {
        std::vector<std::byte> data;

        constexpr iota_map<4, 1> componentCountMap;
        constexpr type_map componentTypeMap {
            make_type_map_entry<std::int8_t>(ComponentType::Byte),
            make_type_map_entry<std::uint8_t>(ComponentType::UnsignedByte),
            make_type_map_entry<std::int16_t>(ComponentType::Short),
            make_type_map_entry<std::uint16_t>(ComponentType::UnsignedShort),
            make_type_map_entry<std::int32_t>(ComponentType::Int),
            make_type_map_entry<std::uint32_t>(ComponentType::UnsignedInt),
            make_type_map_entry<float>(ComponentType::Float),
        };
        std::visit([&]<typename ComponentType>(auto ComponentCount, std::type_identity<ComponentType>) {
            using ElementType = std::conditional_t<ComponentCount == 1, ComponentType, math::vec<ComponentType, ComponentCount>>;

            constexpr std::size_t AlignedElementSize = (sizeof(ElementType) / 4 + (sizeof(ElementType) % 4 != 0)) * 4;
            // Instead of use tight size (AlignedElementSize * (accessor.count - 1) + sizeof(ElementType)), 4-byte
            // aligned padding at the end can make shaders can safely obtain {i|u}{8|16}vec3 from fetched {i|u}{8|16}vec4.
            data.resize(AlignedElementSize * accessor.count);

            copyFromAccessor<ElementType, AlignedElementSize>(asset, accessor, data.data(), adapter);
        }, componentCountMap.get_variant(getNumComponents(accessor.type)), componentTypeMap.get_variant(accessor.componentType));

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
    [[nodiscard]] std::span<float> getTargetWeights(Node &node, Asset &asset) noexcept;

    /**
     * @copydoc getTargetWeights
     */
    export
    [[nodiscard]] std::span<const float> getTargetWeights(const Node &node, const Asset &asset) noexcept;

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
    [[nodiscard]] std::size_t getTargetWeightCount(const Node &node, const Asset &asset) noexcept;

    /**
     * Traverse node's descendants using preorder traversal.
     * @tparam F Function type that can be executed with node index. If it returns contextually convertible to <tt>bool</tt> type, the return value will be determined as the traversal continuation (<tt>true</tt> -> continue traversal).
     * @param asset fastgltf Asset.
     * @param scene Node index to start traversal.
     * @param f Function that would be invoked with node index.
     */
    export template <std::invocable<std::size_t> F>
    void traverseNode(const Asset &asset, std::size_t nodeIndex, const F &f) noexcept(std::is_nothrow_invocable_v<F, std::size_t>) {
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
    export template <std::invocable<std::size_t, const math::fmat4x4&> F>
    void traverseNode(const Asset &asset, std::size_t nodeIndex, const F &f, const math::fmat4x4 &initialNodeWorldTransform) noexcept(std::is_nothrow_invocable_v<F, std::size_t, const math::fmat4x4&>) {
        [&](this const auto &self, std::size_t nodeIndex, const math::fmat4x4 &worldTransform) -> void {
            // If F is predicate, traversal continuation is determined by the return value of f.
            if constexpr (std::predicate<F, std::size_t, const math::fmat4x4&>) {
                // Stop traversal if f returns false.
                if (!f(nodeIndex, worldTransform)) return;
            }
            else {
                f(nodeIndex, worldTransform);
            }

            for (std::size_t childNodeIndex : asset.nodes[nodeIndex].children) {
                const math::fmat4x4 childNodeWorldTransform = getTransformMatrix(asset.nodes[childNodeIndex], worldTransform);
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
    void traverseScene(const Asset &asset, const Scene &scene, const F &f) noexcept(std::is_nothrow_invocable_v<F, std::size_t>) {
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
    export template <std::invocable<std::size_t, const math::fmat4x4&> F>
    void traverseScene(const Asset &asset, const Scene &scene, const F &f) noexcept(std::is_nothrow_invocable_v<F, std::size_t, const math::fmat4x4&>) {
        for (std::size_t nodeIndex : scene.nodeIndices) {
            traverseNode(asset, nodeIndex, f, getTransformMatrix(asset.nodes[nodeIndex]));
        }
    }

    /**
     * @brief Get min/max points of \p primitive's bounding box.
     *
     * @param primtiive primitive to get the bounding box corner points.
     * @param node Node that owns \p primitive.
     * @param asset Asset that owns \p node.
     * @return Array of (min, max) of the bounding box.
     * @note Skinned meshes are not supported, as the bounding box of skinned meshes cannot be determined by the primitive's <tt>POSITION</tt> accessor min/max values.
     */
    export
    [[nodiscard]] std::array<math::fvec3, 2> getBoundingBoxMinMax(const Primitive &primitive, const Node &node, const Asset &asset);

    /**
     * @brief Get 8 corner points of \p primitive's bounding box, which are ordered by:
     * - (minX, minY, minZ)
     * - (minX, minY, maxZ)
     * - (minX, maxY, minZ)
     * - (minX, maxY, maxZ)
     * - (maxX, minY, minZ)
     * - (maxX, minY, maxZ)
     * - (maxX, maxY, minZ)
     * - (maxX, maxY, maxZ)
     *
     * @param primtiive primitive to get the bounding box corner points.
     * @param node Node that owns \p primitive.
     * @param asset Asset that owns \p node.
     * @return Array of 8 corner points of the bounding box.
     * @note Skinned meshes are not supported, as the bounding box of skinned meshes cannot be determined by the primitive's <tt>POSITION</tt> accessor min/max values.
     */
    export
    [[nodiscard]] std::array<math::fvec3, 8> getBoundingBoxCornerPoints(const Primitive &primitive, const Node &node, const Asset &asset);

    /**
     * @brief Create association of (mapping index) -> [(primitive, material index)] for <tt>KHR_materials_variants</tt>.
     *
     * <tt>KHR_materials_variants</tt> extension defines the material variants for each primitive. For each variant index, you
     * can call `at` to get the list of primitives and their material indices that use the corresponding material variant.
     *
     * @param asset fastgltf Asset.
     * @return <tt>std::unordered_map</tt> of (mapping index) -> [(primitive, material index)].
     */
    export std::unordered_map<std::size_t, std::vector<std::pair<Primitive*, std::size_t>>> getMaterialVariantsMapping(Asset &asset LIFETIMEBOUND);

namespace math {
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

    // fastgltf::math::affineInverse is not exported in fastgltf module, but MSVC pretends it to be.
    // TODO: remove the workaround when fixed.
#ifndef _MSC_VER
    /**
     * @brief Get inverse of \p m, assuming it is Affine.
     *
     * The statement "a matrix is Affine" means the matrix can be obtained by applying a finite count of translation,
     * rotation or scale transformation to the identity matrix.
     *
     * Since glTF mandates a node's local transformation matrix to be Affine, so does its world transformation matrix,
     * this function can be used for get their inverse matrices with relatively cheap cost.
     *
     * @tparam T Matrix type.
     * @param m Affine matrix.
     * @return Inverse of \p m.
     */
    export template <typename T>
    [[nodiscard]] constexpr mat<T, 4, 4> affineInverse(const mat<T, 4, 4> &m) noexcept {
        const mat<T, 3, 3> inv = inverse(mat<T, 3, 3> { m });
        const vec<T, 3> l = -inv * vec<T, 3> { m.col(3) };
        return mat<T, 4, 4> {
            vec<T, 4> { inv.col(0).x(), inv.col(0).y(), inv.col(0).z(), 0 },
            vec<T, 4> { inv.col(1).x(), inv.col(1).y(), inv.col(1).z(), 0 },
            vec<T, 4> { inv.col(2).x(), inv.col(2).y(), inv.col(2).z(), 0 },
            vec<T, 4> { l.x(), l.y(), l.z(), 1 },
        };
    }
#endif
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

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

cpp_util::cstring_view fastgltf::to_string(PrimitiveType value) noexcept {
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

cpp_util::cstring_view fastgltf::to_string(AccessorType value) noexcept {
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

cpp_util::cstring_view fastgltf::to_string(ComponentType value) noexcept {
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

cpp_util::cstring_view fastgltf::to_string(BufferTarget target) noexcept {
    switch (target) {
        case BufferTarget::ArrayBuffer: return "ArrayBuffer";
        case BufferTarget::ElementArrayBuffer: return "ElementArrayBuffer";
        default: return "-";
    }
}

cpp_util::cstring_view fastgltf::to_string(MimeType mime) noexcept {
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

cpp_util::cstring_view fastgltf::to_string(AlphaMode alphaMode) noexcept {
    switch (alphaMode) {
        case AlphaMode::Opaque: return "Opaque";
        case AlphaMode::Mask: return "Mask";
        case AlphaMode::Blend: return "Blend";
    }
    std::unreachable();
}

cpp_util::cstring_view fastgltf::to_string(Filter filter) noexcept {
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

cpp_util::cstring_view fastgltf::to_string(Wrap wrap) noexcept {
    switch (wrap) {
        case Wrap::Repeat: return "Repeat";
        case Wrap::ClampToEdge: return "ClampToEdge";
        case Wrap::MirroredRepeat: return "MirroredRepeat";
    }
    std::unreachable();
}

cpp_util::cstring_view fastgltf::to_string(AnimationPath path) noexcept {
    switch (path) {
        case AnimationPath::Translation: return "translation";
        case AnimationPath::Rotation: return "rotation";
        case AnimationPath::Scale: return "scale";
        case AnimationPath::Weights: return "weights";
    }
    std::unreachable();
}

cpp_util::cstring_view fastgltf::to_string(AnimationInterpolation interpolation) noexcept {
    switch (interpolation) {
        case AnimationInterpolation::Linear: return "LINEAR";
        case AnimationInterpolation::Step: return "STEP";
        case AnimationInterpolation::CubicSpline: return "CUBICSPLINE";
    }
    std::unreachable();
}

fastgltf::math::fmat4x4 fastgltf::toMatrix(const TRS &trs, const math::fmat4x4 &matrix) noexcept {
    return scale(rotate(translate(matrix, trs.translation), trs.rotation), trs.scale);
}

std::size_t fastgltf::getTexcoordIndex(const TextureInfo &textureInfo) noexcept {
    if (textureInfo.transform && textureInfo.transform->texCoordIndex) {
        return *textureInfo.transform->texCoordIndex;
    }
    return textureInfo.texCoordIndex;
}

std::size_t fastgltf::getPreferredImageIndex(const Texture &texture) {
    return to_optional(texture.basisuImageIndex) // Prefer BasisU compressed image if exists.
        .or_else([&]() { return to_optional(texture.imageIndex); }) // Otherwise, use regular image.
        .value();
}

std::span<float> fastgltf::getTargetWeights(Node &node, Asset &asset) noexcept {
    std::span weights = node.weights;
    if (node.meshIndex) {
        weights = asset.meshes[*node.meshIndex].weights;
    }
    return weights;
}

std::span<const float> fastgltf::getTargetWeights(const Node &node, const Asset &asset) noexcept {
    std::span weights = node.weights;
    if (node.meshIndex) {
        weights = asset.meshes[*node.meshIndex].weights;
    }
    return weights;
}

std::size_t fastgltf::getTargetWeightCount(const Node &node, const Asset &asset) noexcept {
    std::size_t count = node.weights.size();
    if (node.meshIndex) {
        count = asset.meshes[*node.meshIndex].weights.size();
    }
    return count;
}

std::array<fastgltf::math::fvec3, 2> fastgltf::getBoundingBoxMinMax(const Primitive &primitive, const Node &node, const Asset &asset) {
    constexpr auto getAccessorMinMax = [](const Accessor &accessor) {
        constexpr auto fetchVec3 = visitor {
            []<typename U>(const std::pmr::vector<U> &v) {
                assert(v.size() == 3);
                return math::fvec3 { static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]) };
            },
            [](std::monostate) -> math::fvec3 {
                throw std::invalid_argument { "Accessor min/max is not number" };
            },
        };

        math::fvec3 min = visit(fetchVec3, accessor.min);
        math::fvec3 max = visit(fetchVec3, accessor.max);

        if (accessor.normalized) {
            switch (accessor.componentType) {
            case ComponentType::Byte:
                min = cwiseMax(min / 127, math::fvec3(-1));
                max = cwiseMax(max / 127, math::fvec3(-1));
                break;
            case ComponentType::UnsignedByte:
                min /= 255;
                max /= 255;
                break;
            case ComponentType::Short:
                min = cwiseMax(min / 32767, math::fvec3(-1));
                max = cwiseMax(max / 32767, math::fvec3(-1));
                break;
            case ComponentType::UnsignedShort:
                min /= 65535;
                max /= 65535;
                break;
            default:
                throw std::logic_error { "Normalized accessor must be either BYTE, UNSIGNED_BYTE, SHORT, or UNSIGNED_SHORT" };
            }
        }
        return std::array { min, max };
    };

    const Accessor &accessor = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex];
    std::array bound = getAccessorMinMax(accessor);

    for (const auto &[weight, attributes] : std::views::zip(getTargetWeights(node, asset), primitive.targets)) {
        for (const auto &[attributeName, accessorIndex] : attributes) {
            using namespace std::string_view_literals;
            if (attributeName == "POSITION"sv) {
                const Accessor &accessor = asset.accessors[accessorIndex];
                std::array offset = getAccessorMinMax(accessor);

                // TODO: is this code valid? Need investigation.
                if (weight < 0) {
                    std::swap(get<0>(offset), get<1>(offset));
                }
                get<0>(bound) += get<0>(offset) * weight;
                get<1>(bound) += get<1>(offset) * weight;

                break;
            }
        }
    }

    return bound;
}

std::array<fastgltf::math::fvec3, 8> fastgltf::getBoundingBoxCornerPoints(const Primitive &primitive, const Node &node, const Asset &asset) {
    const auto [min, max] = getBoundingBoxMinMax(primitive, node, asset);
    return {
        min,
        { min[0], min[1], max[2] },
        { min[0], max[1], min[2] },
        { min[0], max[1], max[2] },
        { max[0], min[1], min[2] },
        { max[0], min[1], max[2] },
        { max[0], max[1], min[2] },
        max,
    };
}

std::unordered_map<std::size_t, std::vector<std::pair<fastgltf::Primitive*, std::size_t>>> fastgltf::getMaterialVariantsMapping(Asset &asset) {
    std::unordered_map<std::size_t, std::vector<std::pair<Primitive*, std::size_t>>> result;
    for (Mesh &mesh : asset.meshes) {
        for (Primitive &primitive : mesh.primitives) {
            for (std::size_t i = 0; const auto &mapping : primitive.mappings) {
                result[i++].emplace_back(&primitive, mapping.value_or(primitive.materialIndex.value()));
            }
        }
    }
    return result;
}