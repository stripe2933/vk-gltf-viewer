export module vk_gltf_viewer:helpers.fastgltf;

import std;
export import cstring_view;
export import fastgltf;
export import :helpers.optional;

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
    [[nodiscard]] auto to_optional(OptionalWithFlagValue<T> v) noexcept -> std::optional<T> {
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
    }

    export
    [[nodiscard]] cpp_util::cstring_view to_string(AnimationInterpolation interpolation) noexcept {
        switch (interpolation) {
            case AnimationInterpolation::Linear: return "LINEAR";
            case AnimationInterpolation::Step: return "STEP";
            case AnimationInterpolation::CubicSpline: return "CUBICSPLINE";
        }
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
     * @brief Get transform matrices of \p node instances.
     *
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view. If you provided <tt>fastgltf::Options::LoadExternalBuffers</tt> to the <tt>fastgltf::Parser</tt> while loading the glTF, the parameter can be omitted.
     * @param asset fastgltf asset.
     * @param node Node to get instance transforms. This MUST be originated from the \p asset.
     * @param adapter Buffer data adapter.
     * @return A vector of instance transform matrices.
     * @note This function has effect only if \p asset is loaded with EXT_mesh_gpu_instancing extension supporting parser (otherwise, it will return the empty vector).
     */
    export template <typename BufferDataAdapter = DefaultBufferDataAdapter>
    [[nodiscard]] std::vector<math::fmat4x4> getInstanceTransforms(const Asset &asset, const Node &node, const BufferDataAdapter &adapter = {}) {
        if (node.instancingAttributes.empty()) {
            // No instance transforms. Returning an empty vector.
            return {};
        }

        // According to the EXT_mesh_gpu_instancing specification, all attribute accessors in a given node must
        // have the same count. Therefore, we can use the count of the first attribute accessor.
        const std::uint32_t instanceCount = asset.accessors[node.instancingAttributes[0].accessorIndex].count;

        std::vector<math::fvec3> translations(instanceCount);
        if (auto it = node.findInstancingAttribute("TRANSLATION"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->accessorIndex];
            copyFromAccessor<math::fvec3>(asset, accessor, translations.data(), adapter);
        }

        std::vector<math::fquat> rotations(instanceCount);
        if (auto it = node.findInstancingAttribute("ROTATION"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->accessorIndex];
            copyFromAccessor<math::fquat>(asset, accessor, rotations.data(), adapter);

            // TODO: why fastgltf::copyFromAccessor does not respect the normalized accessor? Need investigation.
            if (accessor.normalized) {
                float multiplier = 1.f / [&]() {
                    switch (accessor.componentType) {
                    case ComponentType::Byte: return 256.f;
                    case ComponentType::Short: return 65536.f;
                    default:
                        // EXT_mesh_gpu_instancing restricts the component type of ROTATION attribute to BYTE
                        // normalized and SHORT normalized only.
                        std::unreachable();
                    }
                }();

                for (math::fquat &rotation : rotations) {
                    rotation *= multiplier;
                }
            }
        }

        std::vector<math::fvec3> scale(instanceCount);
        if (auto it = node.findInstancingAttribute("SCALE"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->accessorIndex];
            fastgltf::copyFromAccessor<math::fvec3>(asset, accessor, scale.data(), adapter);
        }

        std::vector<math::fmat4x4> result;
        result.reserve(instanceCount);
        for (std::uint32_t i = 0; i < instanceCount; ++i) {
            constexpr math::fmat4x4 identity { 1.f };
            result.push_back(translate(identity, translations[i]) * rotate(identity, rotations[i]) * math::scale(identity, scale[i]));
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
    template <std::move_constructible T>
    [[nodiscard]] T get_checked(Expected<T> expected) {
        if (expected) {
            return std::move(expected.get());
        }
        throw expected.error();
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