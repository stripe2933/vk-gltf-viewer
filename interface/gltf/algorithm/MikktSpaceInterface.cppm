module;

#include <mikktspace.h>

export module vk_gltf_viewer.gltf.algorithm.MikktSpaceInterface;

import std;
export import fastgltf;
export import vk_gltf_viewer.gltf.AssetExternalBuffers;

import vk_gltf_viewer.helpers.fastgltf;

namespace vk_gltf_viewer::gltf::algorithm {
    export class MikktSpaceMesh {
    public:
        std::reference_wrapper<const fastgltf::Asset> asset;
        std::reference_wrapper<const fastgltf::Primitive> primitive;

        std::vector<fastgltf::math::s8vec4> tangents;

        MikktSpaceMesh(const fastgltf::Asset &asset, const fastgltf::Primitive &primitive, const AssetExternalBuffers &adapter);

        [[nodiscard]] int getFaceCount() const;
        [[nodiscard]] std::uint32_t getIndex(int face, int vertex) const;
        [[nodiscard]] fastgltf::math::fvec3 getPosition(std::size_t n) const;
        [[nodiscard]] fastgltf::math::fvec3 getNormal(std::size_t n) const;
        [[nodiscard]] fastgltf::math::fvec2 getTexcoord(std::size_t n) const;

        [[nodiscard]] static int getNumVertexPerFaceCount() noexcept;

    private:
        std::reference_wrapper<const fastgltf::Accessor> indicesAccessor;
        std::reference_wrapper<const fastgltf::Accessor> positionAccessor;
        std::reference_wrapper<const fastgltf::Accessor> normalAccessor;
        std::reference_wrapper<const fastgltf::Accessor> texcoordAccessor;

        std::reference_wrapper<const AssetExternalBuffers> bufferDataAdapter;
    };

    export struct MikktSpaceInterface : SMikkTSpaceInterface {
        MikktSpaceInterface();
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::gltf::algorithm ::MikktSpaceMesh::MikktSpaceMesh(const fastgltf::Asset &asset, const fastgltf::Primitive &primitive, const AssetExternalBuffers &adapter)
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

int vk_gltf_viewer::gltf::algorithm::MikktSpaceMesh::getFaceCount() const {
    switch (primitive.get().type) {
        case fastgltf::PrimitiveType::Triangles:
            return indicesAccessor.get().count / 3;
        case fastgltf::PrimitiveType::TriangleStrip:
        case fastgltf::PrimitiveType::TriangleFan:
            return indicesAccessor.get().count - 2;
        default:
            throw std::runtime_error { "Tangent generation using MikkTSpace only supports triangle topology" };
    }
}

std::uint32_t vk_gltf_viewer::gltf::algorithm::MikktSpaceMesh::getIndex(int face, int vertex) const {
    const std::size_t fetchIndex = [&]() {
        switch (primitive.get().type) {
            case fastgltf::PrimitiveType::Triangles:
                return 3 * face + vertex;
            case fastgltf::PrimitiveType::TriangleStrip:
                return face + vertex;
            case fastgltf::PrimitiveType::TriangleFan:
                return (vertex == 0 ? 0 : face) + vertex;
            default:
                // Will be handled in getFaceCount, therefore could be optimized in here.
                std::unreachable();
        }
    }();
    return fastgltf::getAccessorElement<std::uint32_t>(asset, indicesAccessor, fetchIndex, bufferDataAdapter);
}

fastgltf::math::fvec3 vk_gltf_viewer::gltf::algorithm::MikktSpaceMesh::getPosition(std::size_t n) const {
    return fastgltf::getAccessorElement<fastgltf::math::fvec3>(asset, positionAccessor, n, bufferDataAdapter);
}

fastgltf::math::fvec3 vk_gltf_viewer::gltf::algorithm::MikktSpaceMesh::getNormal(std::size_t n) const {
    return fastgltf::getAccessorElement<fastgltf::math::fvec3>(asset, normalAccessor, n, bufferDataAdapter);
}

fastgltf::math::fvec2 vk_gltf_viewer::gltf::algorithm::MikktSpaceMesh::getTexcoord(std::size_t n) const {
    return fastgltf::getAccessorElement<fastgltf::math::fvec2>(asset, texcoordAccessor, n, bufferDataAdapter);
}

int vk_gltf_viewer::gltf::algorithm::MikktSpaceMesh::getNumVertexPerFaceCount() noexcept {
    return 3;
}

vk_gltf_viewer::gltf::algorithm::MikktSpaceInterface::MikktSpaceInterface()
    : SMikkTSpaceInterface {
        .m_getNumFaces = [](const SMikkTSpaceContext *pContext) -> int {
            const auto *meshData = static_cast<const MikktSpaceMesh*>(pContext->m_pUserData);
            return meshData->getFaceCount();
        },
        .m_getNumVerticesOfFace = [](const SMikkTSpaceContext *pContext, int) {
            const auto *meshData = static_cast<const MikktSpaceMesh*>(pContext->m_pUserData);
            return meshData->getNumVertexPerFaceCount();
        },
        .m_getPosition = [](const SMikkTSpaceContext *pContext, float fvPosOut[], int iFace, int iVert) {
            const auto *meshData = static_cast<const MikktSpaceMesh*>(pContext->m_pUserData);
            std::copy_n(meshData->getPosition(meshData->getIndex(iFace, iVert)).data(), 3, fvPosOut);
        },
        .m_getNormal = [](const SMikkTSpaceContext *pContext, float fvNormOut[], int iFace, int iVert) {
            const auto *meshData = static_cast<const MikktSpaceMesh*>(pContext->m_pUserData);
            std::copy_n(meshData->getNormal(meshData->getIndex(iFace, iVert)).data(), 3, fvNormOut);
        },
        .m_getTexCoord = [](const SMikkTSpaceContext *pContext, float fvTexcOut[], int iFace, int iVert) {
            const auto *meshData = static_cast<const MikktSpaceMesh*>(pContext->m_pUserData);
            std::copy_n(meshData->getTexcoord(meshData->getIndex(iFace, iVert)).data(), 2, fvTexcOut);
        },
        .m_setTSpaceBasic = [](const SMikkTSpaceContext *pContext, const float *fvTangent, float fSign, int iFace, int iVert) {
            auto *meshData = static_cast<MikktSpaceMesh*>(pContext->m_pUserData);
            meshData->tangents[meshData->getIndex(iFace, iVert)] = {
                static_cast<std::int8_t>(fvTangent[0] * 127.0f),
                static_cast<std::int8_t>(fvTangent[1] * 127.0f),
                static_cast<std::int8_t>(fvTangent[2] * 127.0f),
                static_cast<std::int8_t>(fSign * 127.0f),
            };
        },
    } { }