export module vk_gltf_viewer:helpers.fastgltf;

import std;
export import fastgltf;
export import glm;
export import :helpers.cstring_view;
export import :helpers.optional;

namespace fastgltf {
    export template <typename T>
    [[nodiscard]] auto to_optional(OptionalWithFlagValue<T> v) noexcept -> std::optional<T> {
        return value_if(v.has_value(), [&]() { return *v; });
    }

    export
    [[nodiscard]] auto to_string(PrimitiveType value) noexcept -> cstring_view {
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
    [[nodiscard]] auto to_string(AccessorType value) noexcept -> cstring_view {
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
    [[nodiscard]] auto to_string(ComponentType value) noexcept -> cstring_view {
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
    [[nodiscard]] auto to_string(BufferTarget target) noexcept -> cstring_view {
        switch (target) {
            case BufferTarget::ArrayBuffer: return "ArrayBuffer";
            case BufferTarget::ElementArrayBuffer: return "ElementArrayBuffer";
            default: return "-";
        }
    }

    export
    [[nodiscard]] auto to_string(MimeType mime) noexcept -> cstring_view {
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
    [[nodiscard]] auto to_string(AlphaMode alphaMode) noexcept -> cstring_view {
        switch (alphaMode) {
            case AlphaMode::Opaque: return "Opaque";
            case AlphaMode::Mask: return "Mask";
            case AlphaMode::Blend: return "Blend";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] auto to_string(Filter filter) noexcept -> cstring_view {
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
    [[nodiscard]] auto to_string(Wrap wrap) noexcept -> cstring_view {
        switch (wrap) {
            case Wrap::Repeat: return "Repeat";
            case Wrap::ClampToEdge: return "ClampToEdge";
            case Wrap::MirroredRepeat: return "MirroredRepeat";
        }
        std::unreachable();
    }

    export
    [[nodiscard]] glm::mat4 toMatrix(const Node::TransformMatrix &transformMatrix) noexcept {
        return glm::make_mat4(transformMatrix.data());
    }

    export
    [[nodiscard]] glm::mat4 toMatrix(const TRS &trs) noexcept {
        return translate(glm::mat4 { 1.f }, glm::make_vec3(trs.translation.data()))
            * mat4_cast(glm::make_quat(trs.rotation.data()))
            * scale(glm::mat4 { 1.f }, glm::make_vec3(trs.scale.data()));
    }

    /**
     * Get buffer byte region of \p bufferView.
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view. If you provided <tt>fastgltf::Options::LoadExternalBuffers</tt> to the <tt>fastgltf::Parser</tt> while loading the glTF, the parameter can be omitted.
     * @param asset fastgltf Asset.
     * @param bufferView Buffer view to get the byte region. This MUST be originated from the \p asset.
     * @param adapter Buffer data adapter.
     * @return Span of bytes.
     */
    export template <typename BufferDataAdapter = DefaultBufferDataAdapter>
    [[nodiscard]] std::span<const std::byte> getByteRegion(const Asset &asset, const BufferView &bufferView, const BufferDataAdapter &adapter = {}) noexcept {
        return std::span { adapter(asset.buffers[bufferView.bufferIndex]) + bufferView.byteOffset, bufferView.byteLength };
    }

    /**
     * Get buffer byte region of \p accessor.
     * @tparam BufferDataAdapter A functor type that acquires the binary buffer data from a glTF buffer view. If you provided <tt>fastgltf::Options::LoadExternalBuffers</tt> to the <tt>fastgltf::Parser</tt> while loading the glTF, the parameter can be omitted.
     * @param asset fastgltf Asset.
     * @param accessor Accessor to get the byte region.
     * @param adapter Buffer data adapter.
     * @return Span of bytes.
     * @throw std::runtime_error If the accessor is sparse or has no buffer view.
     */
    export template <typename BufferDataAdapter = DefaultBufferDataAdapter>
    [[nodiscard]] std::span<const std::byte> getByteRegion(const Asset &asset, const Accessor &accessor, const BufferDataAdapter &adapter = {}) {
        if (accessor.sparse) throw std::runtime_error { "Sparse accessor not supported." };
        if (!accessor.bufferViewIndex) throw std::runtime_error { "Accessor has no buffer view." };

        const BufferView &bufferView = asset.bufferViews[*accessor.bufferViewIndex];
        const std::size_t byteStride = bufferView.byteStride.value_or(getElementByteSize(accessor.type, accessor.componentType));
        return getByteRegion(asset, bufferView, adapter).subspan(accessor.byteOffset, byteStride * accessor.count);
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
    [[nodiscard]] std::vector<glm::mat4> getInstanceTransforms(const Asset &asset, const Node &node, const BufferDataAdapter &adapter = {}) {
        if (node.instancingAttributes.empty()) {
            // No instance transforms. Returning an empty vector.
            return {};
        }

        // According to the EXT_mesh_gpu_instancing specification, all attribute accessors in a given node must
        // have the same count. Therefore, we can use the count of the first attribute accessor.
        const std::uint32_t instanceCount = asset.accessors[node.instancingAttributes[0].second].count;

        std::vector translations(instanceCount, glm::vec3 { 0.f });
        if (auto it = node.findInstancingAttribute("TRANSLATION"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->second];
            copyFromAccessor<glm::vec3>(asset, accessor, translations.data(), adapter);
        }

        std::vector rotations(instanceCount, glm::vec4 { 0.f, 0.f, 0.f, 1.f });
        if (auto it = node.findInstancingAttribute("ROTATION"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->second];
            copyFromAccessor<glm::vec4>(asset, accessor, rotations.data(), adapter);

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

                for (glm::vec4 &rotation : rotations) {
                    rotation *= multiplier;
                }
            }
        }

        std::vector scale(instanceCount, glm::vec3 { 1.f, 1.f, 1.f });
        if (auto it = node.findInstancingAttribute("SCALE"); it != node.instancingAttributes.end()) {
            const Accessor &accessor = asset.accessors[it->second];
            fastgltf::copyFromAccessor<glm::vec3>(asset, accessor, scale.data(), adapter);
        }

        std::vector<glm::mat4> result;
        result.reserve(instanceCount);
        for (std::uint32_t i = 0; i < instanceCount; ++i) {
            result.push_back(
                glm::translate(glm::mat4 { 1.f }, translations[i])
                    * mat4_cast(glm::make_quat(value_ptr(rotations[i])))
                    * glm::scale(glm::mat4 { 1.f }, scale[i]));
        }

        return result;
    }
}
