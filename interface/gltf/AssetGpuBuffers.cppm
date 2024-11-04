export module vk_gltf_viewer:gltf.AssetGpuBuffers;

import std;
export import glm;
import thread_pool;
export import :gltf.AssetExternalBuffers;
export import :gltf.AssetPrimitiveInfo;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief GPU buffers for <tt>fastgltf::Asset</tt>.
     *
     * These buffers could be used for all asset. If you're finding the scene specific buffers (like node transformation matrices, ordered node primitives, etc.), see AssetSceneGpuBuffers for that purpose.
     */
    export class AssetGpuBuffers {
        const fastgltf::Asset &asset;
        const vulkan::Gpu &gpu;

        /**
         * @brief Ordered asset primitives.
         *
         * glTF asset primitives are appeared inside the mesh, and they have no explicit ordering. This vector is constructed by traversing the asset meshes and collect the primitives with their appearing order.
         */
        std::vector<const fastgltf::Primitive*> orderedPrimitives = createOrderedPrimitives();

        /**
         * @brief GPU buffers that would only be accessed by buffer device address.
         *
         * Asset buffer view data that are used by attributes, missing tangents, indexed attribute (e.g. <tt>TEXCOORD_<i></tt>) mapping information are staged into GPU buffer, but these are "unnamed". They are specific to this class' implementation, and cannot be accessed from outside this class. Instead, their device addresses are stored in AssetPrimitiveInfo and could be accessed in the shader.
         */
        std::vector<vku::AllocatedBuffer> internalBuffers;

        /**
         * @brief Staging buffers and their copy infos.
         *
         * Consisted of <tt>AllocatedBuffer</tt> that contains the data, <tt>Buffer</tt> that will be copied into, and <tt>BufferCopy</tt> that describes the copy region.
         * This should be cleared after the staging operation ends.
         */
        std::vector<std::tuple<vku::AllocatedBuffer, vk::Buffer, vk::BufferCopy>> stagingInfos;

    public:
        struct GpuMaterial {
            std::uint8_t baseColorTexcoordIndex;
            std::uint8_t metallicRoughnessTexcoordIndex;
            std::uint8_t normalTexcoordIndex;
            std::uint8_t occlusionTexcoordIndex;
            std::uint8_t emissiveTexcoordIndex;
            char padding0[1];
            std::int16_t baseColorTextureIndex = -1;
            std::int16_t metallicRoughnessTextureIndex = -1;
            std::int16_t normalTextureIndex = -1;
            std::int16_t occlusionTextureIndex = -1;
            std::int16_t emissiveTextureIndex = -1;
            glm::vec4 baseColorFactor = { 1.f, 1.f, 1.f, 1.f };
            float metallicFactor = 1.f;
            float roughnessFactor = 1.f;
            float normalScale = 1.f;
            float occlusionStrength = 1.f;
            glm::vec3 emissiveFactor = { 0.f, 0.f, 0.f };
            float alphaCutOff;
        };

        struct GpuPrimitive {
            vk::DeviceAddress pPositionBuffer;
            vk::DeviceAddress pNormalBuffer;
            vk::DeviceAddress pTangentBuffer;
            vk::DeviceAddress pTexcoordAttributeMappingInfoBuffer;
            vk::DeviceAddress pColorAttributeMappingInfoBuffer;
            std::uint8_t positionByteStride;
            std::uint8_t normalByteStride;
            std::uint8_t tangentByteStride;
            char padding[1];
            std::uint32_t materialIndex;
        };

        std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> primitiveInfos = createPrimitiveInfos();

        /**
         * @brief Buffer that contains <tt>GpuMaterial</tt>s, with fallback material at the index 0 (total <tt>asset.materials.size() + 1</tt>).
         */
        vku::AllocatedBuffer materialBuffer = createMaterialBuffer();

        /**
         * @brief All indices that are combined as a single buffer by their index types.
         *
         * Use <tt>AssetPrimitiveInfo::indexInfo</tt> to get the offset of data that is used by the primitive.
         *
         * @note If you passed <tt>vulkan::Gpu</tt> whose <tt>supportUint8Index</tt> is <tt>false</tt>, primitive with unsigned byte (<tt>uint8_t</tt>) indices will be converted to unsigned short (<tt>uint16_t</tt>) indices.
         */
        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> indexBuffers;

        vku::AllocatedBuffer primitiveBuffer = createPrimitiveBuffer();

        AssetGpuBuffers(const fastgltf::Asset &asset, const AssetExternalBuffers &externalBuffers, const vulkan::Gpu &gpu, BS::thread_pool threadPool = {});

        /**
         * @brief Get the primitive by its order, which has the same manner of <tt>primitiveBuffer</tt>.
         * @param index The order of the primitive.
         * @return The primitive.
         */
        [[nodiscard]] const fastgltf::Primitive &getPrimitiveByOrder(std::uint16_t index) const { return *orderedPrimitives[index]; }

    private:
        [[nodiscard]] std::vector<const fastgltf::Primitive*> createOrderedPrimitives() const;
        [[nodiscard]] std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> createPrimitiveInfos() const;
        [[nodiscard]] vku::AllocatedBuffer createMaterialBuffer();
        [[nodiscard]] std::unordered_map<vk::IndexType, vku::AllocatedBuffer> createPrimitiveIndexBuffers(const AssetExternalBuffers &externalBuffers);
        [[nodiscard]] vku::AllocatedBuffer createPrimitiveBuffer();

        void createPrimitiveAttributeBuffers(const AssetExternalBuffers &externalBuffers);
        void createPrimitiveIndexedAttributeMappingBuffers();
        void createPrimitiveTangentBuffers(const AssetExternalBuffers &externalBuffers, BS::thread_pool &threadPool);
    };
}