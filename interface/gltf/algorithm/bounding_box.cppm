export module vk_gltf_viewer:gltf.algorithm.bounding_box;

import std;
export import fastgltf;
export import glm;

namespace vk_gltf_viewer::gltf::algorithm {
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
     * @param asset fastgltf asset.
     * @param primtiive fastgltf primitive. This must be originated from \p asset.
     * @return Array of 8 corner points of the bounding box.
     */
    export
    [[nodiscard]] std::array<glm::dvec3, 8> getBoundingBoxCornerPoints(const fastgltf::Asset &asset, const fastgltf::Primitive &primitive) {
        const fastgltf::Accessor &accessor = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex];

        // TODO: current glTF specification guarantees that there are min/max attributes for POSITION with
        //  dvec3 type, but KHR_mesh_quantization extension offers non-double precision POSITION attributes,
        //  which would be problematic in future. Need caution.
        const std::span min { std::get_if<std::pmr::vector<double>>(&accessor.min)->data(), 3 };
        const std::span max { std::get_if<std::pmr::vector<double>>(&accessor.max)->data(), 3 };

        return {
            glm::dvec3 { min[0], min[1], min[2] },
            glm::dvec3 { min[0], min[1], max[2] },
            glm::dvec3 { min[0], max[1], min[2] },
            glm::dvec3 { min[0], max[1], max[2] },
            glm::dvec3 { max[0], min[1], min[2] },
            glm::dvec3 { max[0], min[1], max[2] },
            glm::dvec3 { max[0], max[1], min[2] },
            glm::dvec3 { max[0], max[1], max[2] },
        };
    }
}