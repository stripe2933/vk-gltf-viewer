module;

#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <mikktspace.h>

export module vk_gltf_viewer:gltf.algorithm.MikktSpaceInterface;

import std;
export import glm;

template <>
struct fastgltf::ElementTraits<glm::vec2> : ElementTraitsBase<glm::vec2, AccessorType::Vec2, float> {};
template <>
struct fastgltf::ElementTraits<glm::vec3> : ElementTraitsBase<glm::vec3, AccessorType::Vec3, float> {};

namespace vk_gltf_viewer::gltf::algorithm {
    export template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    struct MikktSpaceMesh {
        const fastgltf::Asset &asset;
        const fastgltf::Accessor &indicesAccessor, &positionAccessor, &normalAccessor, &texcoordAccessor;
        const BufferDataAdapter &bufferDataAdaptor;
        std::vector<glm::vec4> tangents = std::vector<glm::vec4>(positionAccessor.count);
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
                    const glm::vec3 position = fastgltf::getAccessorElement<glm::vec3>(
                        meshData->asset, meshData->positionAccessor, getIndex(*meshData, iFace, iVert), meshData->bufferDataAdaptor);
                    std::ranges::copy_n(value_ptr(position), 3, fvPosOut);
                },
                .m_getNormal = [](const SMikkTSpaceContext *pContext, float fvNormOut[], int iFace, int iVert) {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    const glm::vec3 normal = fastgltf::getAccessorElement<glm::vec3>(
                        meshData->asset, meshData->normalAccessor, getIndex(*meshData, iFace, iVert), meshData->bufferDataAdaptor);
                    std::ranges::copy_n(value_ptr(normal), 3, fvNormOut);
                },
                .m_getTexCoord = [](const SMikkTSpaceContext *pContext, float fvTexcOut[], int iFace, int iVert) {
                    const auto *meshData = static_cast<const MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    const glm::vec2 texcoord = fastgltf::getAccessorElement<glm::vec2>(
                        meshData->asset, meshData->texcoordAccessor, getIndex(*meshData, iFace, iVert), meshData->bufferDataAdaptor);
                    std::ranges::copy_n(value_ptr(texcoord), 2, fvTexcOut);
                },
                .m_setTSpaceBasic = [](const SMikkTSpaceContext *pContext, const float *fvTangent, float fSign, int iFace, int iVert) {
                    auto *meshData = static_cast<MikktSpaceMesh<BufferDataAdapter>*>(pContext->m_pUserData);
                    *std::ranges::copy_n(fvTangent, 3, value_ptr(meshData->tangents[getIndex(*meshData, iFace, iVert)])).out = fSign;
                },
            } { }

        [[nodiscard]] static auto getIndex(const MikktSpaceMesh<BufferDataAdapter> &meshData, int iFace, int iVert) -> int {
            return fastgltf::getAccessorElement<IndexType>(
                meshData.asset, meshData.indicesAccessor, 3 * iFace + iVert, meshData.bufferDataAdaptor);
        }
    };

    export template <std::unsigned_integral T, typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
    MikktSpaceInterface<T, BufferDataAdapter> mikktSpaceInterface;
}