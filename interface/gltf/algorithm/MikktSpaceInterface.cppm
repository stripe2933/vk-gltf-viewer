module;

#include <mikktspace.h>

export module vk_gltf_viewer:gltf.algorithm.MikktSpaceInterface;

import std;
export import fastgltf;

namespace vk_gltf_viewer::gltf::algorithm {
    export template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    struct MikktSpaceMesh {
        const fastgltf::Asset &asset;
        const fastgltf::Accessor &indicesAccessor, &positionAccessor, &normalAccessor, &texcoordAccessor;
        const BufferDataAdapter &bufferDataAdapter;
        std::vector<fastgltf::math::fvec4> tangents = std::vector<fastgltf::math::fvec4>(positionAccessor.count);
    };

    template <std::unsigned_integral IndexType, typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    struct MikktSpaceInterface : SMikkTSpaceInterface {
        constexpr MikktSpaceInterface()
            : SMikkTSpaceInterface {
                .m_getNumFaces = [](const SMikkTSpaceContext *pContext) -> int {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    return meshData->indicesAccessor.count / 3;
                },
                .m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) {
                    return 3; // TODO: support for non-triangle primitive?
                },
                .m_getPosition = [](const SMikkTSpaceContext *pContext, float fvPosOut[], int iFace, int iVert) {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    const fastgltf::math::fvec3 position = fastgltf::getAccessorElement<fastgltf::math::fvec3>(
                        meshData->asset,
                        meshData->positionAccessor,
                        getIndex(*meshData, iFace, iVert),
                        meshData->bufferDataAdapter);
                    std::copy_n(position.data(), 3, fvPosOut);
                },
                .m_getNormal = [](const SMikkTSpaceContext *pContext, float fvNormOut[], int iFace, int iVert) {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    const fastgltf::math::fvec3 normal = fastgltf::getAccessorElement<fastgltf::math::fvec3>(
                        meshData->asset,
                        meshData->normalAccessor,
                        getIndex(*meshData, iFace, iVert),
                        meshData->bufferDataAdapter);
                    std::copy_n(normal.data(), 3, fvNormOut);
                },
                .m_getTexCoord = [](const SMikkTSpaceContext *pContext, float fvTexcOut[], int iFace, int iVert) {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    const fastgltf::math::fvec2 texcoord = fastgltf::getAccessorElement<fastgltf::math::fvec2>(
                        meshData->asset,
                        meshData->texcoordAccessor,
                        getIndex(*meshData, iFace, iVert),
                        meshData->bufferDataAdapter);
                    std::copy_n(texcoord.data(), 2, fvTexcOut);
                },
                .m_setTSpaceBasic = [](const SMikkTSpaceContext *pContext, const float *fvTangent, float fSign, int iFace, int iVert) {
                    auto *meshData = static_cast<MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    *std::copy_n(fvTangent, 3, meshData->tangents[getIndex(*meshData, iFace, iVert)].data()) = fSign;
                },
            } { }

        [[nodiscard]] static auto getIndex(const MikktSpaceMesh<BufferDataAdapter> &meshData, int iFace, int iVert) -> int {
            return fastgltf::getAccessorElement<IndexType>(
                meshData.asset, meshData.indicesAccessor, 3 * iFace + iVert, meshData.bufferDataAdapter);
        }
    };

    export template <std::unsigned_integral T, typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    MikktSpaceInterface<T, BufferDataAdapter> mikktSpaceInterface;
}