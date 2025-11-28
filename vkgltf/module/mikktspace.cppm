module;

#include <mikktspace.h>

export module vkgltf.mikktspace;

import std;
export import fastgltf;

import vkgltf.util;

export template <typename T, typename... Ts>
concept one_of = (std::same_as<T, Ts> || ...);

namespace vkgltf {
    template <typename T, typename BufferDataAdapter>
    struct PrimitiveInfo {
        const fastgltf::Asset &asset;

        const fastgltf::Accessor &positionAccessor;
        const fastgltf::Accessor &normalAccessor;
        const fastgltf::Accessor &texcoordAccessor;
        const fastgltf::Accessor &indicesAccessor;

        fastgltf::PrimitiveType type;
        int faceCount;

        std::vector<fastgltf::math::vec<T, 4>> tangents;

        const BufferDataAdapter &adapter;

        PrimitiveInfo(const fastgltf::Asset &asset, const fastgltf::Primitive &primitive, const BufferDataAdapter &adapter)
            : asset { asset }
            , positionAccessor { asset.accessors[primitive.findAttribute("POSITION")->accessorIndex] }
            , normalAccessor { asset.accessors[primitive.findAttribute("NORMAL")->accessorIndex] }
            , texcoordAccessor { asset.accessors[primitive.findAttribute(std::format("TEXCOORD_{}", utils::getTexcoordIndex(asset.materials[*primitive.materialIndex].normalTexture.value())))->accessorIndex] }
            , indicesAccessor { asset.accessors[primitive.indicesAccessor.value()] }
            , type { primitive.type }
            , adapter { adapter } {
            switch (primitive.type) {
                case fastgltf::PrimitiveType::Triangles:
                    faceCount = indicesAccessor.count / 3;
                    break;
                case fastgltf::PrimitiveType::TriangleStrip:
                case fastgltf::PrimitiveType::TriangleFan:
                    faceCount = indicesAccessor.count - 2;
                    break;
                default:
                    throw std::runtime_error { "Non-triangle topology is unsupported" };
            }
            tangents.resize(positionAccessor.count);
        }

        [[nodiscard]] std::size_t getFetchIndex(int iFace, int iVert) const noexcept {
            switch (type) {
                case fastgltf::PrimitiveType::Triangles:
                    return 3 * iFace + iVert;
                case fastgltf::PrimitiveType::TriangleStrip:
                    return iFace + iVert;
                case fastgltf::PrimitiveType::TriangleFan:
                    return (iVert == 0 ? 0 : iFace) + iVert;
                default:
                    std::unreachable();
            }
        }

        [[nodiscard]] int getIndex(int iFace, int iVert) const noexcept {
            return fastgltf::getAccessorElement<int>(asset, indicesAccessor, getFetchIndex(iFace, iVert), adapter);
        }
    };

    /**
     *
     * @tparam T Component type of the tangent vector. Can be <tt>float</tt>, <tt>std::int16_t</tt>, or <tt>std::int8_t</tt>.
     * @tparam BufferDataAdapter A functor type that return the bytes span from a glTF buffer view.
     * @param asset glTF asset that owns \p primitive.
     * @param primitive Primitive to create tangents for. Must be indexed, and have <tt>POSITION</tt>, <tt>NORMAL</tt>, and <tt>TEXCOORD_<i></tt> (where <i> corresponds to the texture coordinate index of the normal texture) attributes.
     * @param adapter Buffer data adapter.
     * @return Vector of tangents, whose size is equal to \p primitive's indices accessor element count and optionally normalized if \tp T is signed integral type.
     */
    export template <one_of<float, std::int16_t, std::int8_t> T, typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    [[nodiscard]] std::vector<fastgltf::math::vec<T, 4>> createMikkTSpaceTangents(const fastgltf::Asset &asset, const fastgltf::Primitive &primitive, const BufferDataAdapter &adapter = {}) {
        PrimitiveInfo<T, BufferDataAdapter> info { asset, primitive, adapter };

        SMikkTSpaceInterface interface {
            .m_getNumFaces = [](const SMikkTSpaceContext *pContext) noexcept {
                return static_cast<const PrimitiveInfo<T, BufferDataAdapter>*>(pContext->m_pUserData)->faceCount;
            },
            .m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) noexcept { return 3; },
            .m_getPosition = [](const SMikkTSpaceContext *pContext, float fvPosOut[], int iFace, int iVert) {
                const PrimitiveInfo<T, BufferDataAdapter> &info = *static_cast<const PrimitiveInfo<T, BufferDataAdapter>*>(pContext->m_pUserData);
                const auto v = getAccessorElement<fastgltf::math::fvec3>(info.asset, info.positionAccessor, info.getIndex(iFace, iVert), info.adapter);
                fvPosOut[0] = v.x();
                fvPosOut[1] = v.y();
                fvPosOut[2] = v.z();
            },
            .m_getNormal = [](const SMikkTSpaceContext *pContext, float fvNormOut[], int iFace, int iVert) {
                const PrimitiveInfo<T, BufferDataAdapter> &info = *static_cast<const PrimitiveInfo<T, BufferDataAdapter>*>(pContext->m_pUserData);
                const auto v = getAccessorElement<fastgltf::math::fvec3>(info.asset, info.normalAccessor, info.getIndex(iFace, iVert), info.adapter);
                fvNormOut[0] = v.x();
                fvNormOut[1] = v.y();
                fvNormOut[2] = v.z();
            },
            .m_getTexCoord = [](const SMikkTSpaceContext *pContext, float fvTexcOut[], int iFace, int iVert) {
                const PrimitiveInfo<T, BufferDataAdapter> &info = *static_cast<const PrimitiveInfo<T, BufferDataAdapter>*>(pContext->m_pUserData);
                const auto v = getAccessorElement<fastgltf::math::fvec2>(info.asset, info.texcoordAccessor, info.getIndex(iFace, iVert), info.adapter);
                fvTexcOut[0] = v.x();
                fvTexcOut[1] = v.y();
            },
            .m_setTSpaceBasic = [](const SMikkTSpaceContext *pContext, const float *fvTangent, float fSign, int iFace, int iVert) {
                PrimitiveInfo<T, BufferDataAdapter> &info = *static_cast<PrimitiveInfo<T, BufferDataAdapter>*>(pContext->m_pUserData);
                fastgltf::math::vec<T, 4> &tangent = info.tangents[info.getIndex(iFace, iVert)];
                if constexpr (std::signed_integral<T>) {
                    // KHR_mesh_quantization:
                    //   When KHR_mesh_quantization extension is supported, the following extra types are allowed for
                    //   storing mesh attributes in addition to the types defined in Section 3.7.2.1.
                    //   ...
                    //   TANGENT: byte normalized, short normalized
                    //   https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_mesh_quantization/README.md#extending-mesh-attributes
                    //   ...
                    //   Implementations should assume following equations are used to get corresponding floating-point
                    //   value f from a normalized integer c and should use the specified equations to encode
                    //   floating-point values to integers after range normalization:
                    //
                    //   | accessor.componentType |        int-to-float        |      float-to-int      |
                    //   |------------------------|----------------------------|------------------------|
                    //   | 5120 (BYTE)            | f = max(c / 127.0, -1.0)   | c = round(f * 127.0)   |
                    //   | 5121 (UNSIGNED_BYTE)   | f = c / 255.0              | c = round(f * 255.0)   |
                    //   | 5122 (SHORT)           | f = max(c / 32767.0, -1.0) | c = round(f * 32767.0) |
                    //   | 5123 (UNSIGNED_SHORT)  | f = c / 65535.0            | c = round(f * 65535.0) |
                    //   https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_mesh_quantization/README.md#encoding-quantized-data
                    constexpr T max = std::numeric_limits<T>::max();
                    tangent.x() = static_cast<T>(fvTangent[0] * max);
                    tangent.y() = static_cast<T>(fvTangent[1] * max);
                    tangent.z() = static_cast<T>(fvTangent[2] * max);
                    tangent.w() = static_cast<T>(fSign * max);
                }
                else {
                    tangent.x() = fvTangent[0];
                    tangent.y() = fvTangent[1];
                    tangent.z() = fvTangent[2];
                    tangent.w() = fSign;
                }
            },
        };

        const SMikkTSpaceContext context { &interface, &info };
        if (!genTangSpaceDefault(&context)) {
            throw std::runtime_error { "Failed to generate tangents" };
        }

        return std::move(info.tangents);
    }

}