module;

#include <mikktspace.h>

export module vk_gltf_viewer:gltf.algorithm.MikktSpaceInterface;

import std;
export import fastgltf;
import :helpers.fastgltf;

namespace vk_gltf_viewer::gltf::algorithm {
    export template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    class MikktSpaceMesh {
    public:
        std::reference_wrapper<const fastgltf::Asset> asset;
        std::reference_wrapper<const fastgltf::Primitive> primitive;

        std::vector<fastgltf::math::fvec4> tangents;

        MikktSpaceMesh(const fastgltf::Asset &asset, const fastgltf::Primitive &primitive, const BufferDataAdapter &adapter = {})
            : asset { asset }
            , primitive { primitive }
            , indicesAccessor { asset.accessors[primitive.indicesAccessor.value()] }
            , positionAccessor { asset.accessors[primitive.findAttribute("POSITION")->accessorIndex] }
            , normalAccessor { asset.accessors[primitive.findAttribute("NORMAL")->accessorIndex] }
            , texcoordAccessor { [&]() -> const fastgltf::Accessor& {
                const std::size_t normalTexcoordIndex = getTexcoordIndex(asset.materials[primitive.materialIndex.value()].normalTexture.value());
                return asset.accessors[primitive.findAttribute(std::format("TEXCOORD_{}", normalTexcoordIndex))->accessorIndex];
            }() }
            , bufferDataAdapter { adapter } {
            tangents.resize(positionAccessor.get().count);
        }

        [[nodiscard]] int getFaceCount() const noexcept {
            return indicesAccessor.get().count / 3; // TODO: support for non-triangle list primitive?
        }

        [[nodiscard]] int getNumVertexPerFaceCount() const noexcept {
            return 3; // TODO: support for non-triangle list primitive?
        }

        [[nodiscard]] std::uint32_t getIndex(int face, int vertex) const {
            // TODO: support for non-triangle list primitive?
            return fastgltf::getAccessorElement<std::uint32_t>(asset, indicesAccessor, 3 * face + vertex, bufferDataAdapter);
        }

        [[nodiscard]] fastgltf::math::fvec3 getPosition(std::size_t n) const {
            return fastgltf::getAccessorElement<fastgltf::math::fvec3>(asset, positionAccessor, n, bufferDataAdapter);
        }

        [[nodiscard]] fastgltf::math::fvec3 getNormal(std::size_t n) const {
            return fastgltf::getAccessorElement<fastgltf::math::fvec3>(asset, normalAccessor, n, bufferDataAdapter);
        }

        [[nodiscard]] fastgltf::math::fvec2 getTexcoord(std::size_t n) const {
            return fastgltf::getAccessorElement<fastgltf::math::fvec2>(asset, texcoordAccessor, n, bufferDataAdapter);
        }

    private:
        std::reference_wrapper<const fastgltf::Accessor> indicesAccessor;
        std::reference_wrapper<const fastgltf::Accessor> positionAccessor;
        std::reference_wrapper<const fastgltf::Accessor> normalAccessor;
        std::reference_wrapper<const fastgltf::Accessor> texcoordAccessor;

        std::reference_wrapper<const BufferDataAdapter> bufferDataAdapter;
    };

    export template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    struct MikktSpaceInterface : SMikkTSpaceInterface {
        constexpr MikktSpaceInterface()
            : SMikkTSpaceInterface {
                .m_getNumFaces = [](const SMikkTSpaceContext *pContext) -> int {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    return meshData->getFaceCount();
                },
                .m_getNumVerticesOfFace = [](const SMikkTSpaceContext *pContext, int) {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    return meshData->getNumVertexPerFaceCount();
                },
                .m_getPosition = [](const SMikkTSpaceContext *pContext, float fvPosOut[], int iFace, int iVert) {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    std::copy_n(meshData->getPosition(meshData->getIndex(iFace, iVert)).data(), 3, fvPosOut);
                },
                .m_getNormal = [](const SMikkTSpaceContext *pContext, float fvNormOut[], int iFace, int iVert) {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    std::copy_n(meshData->getNormal(meshData->getIndex(iFace, iVert)).data(), 3, fvNormOut);
                },
                .m_getTexCoord = [](const SMikkTSpaceContext *pContext, float fvTexcOut[], int iFace, int iVert) {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    std::copy_n(meshData->getTexcoord(meshData->getIndex(iFace, iVert)).data(), 2, fvTexcOut);
                },
                .m_setTSpaceBasic = [](const SMikkTSpaceContext *pContext, const float *fvTangent, float fSign, int iFace, int iVert) {
                    auto *meshData = static_cast<MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    *std::copy_n(fvTangent, 3, meshData->tangents[meshData->getIndex(iFace, iVert)].data()) = fSign;
                },
            } { }
    };
}