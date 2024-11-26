export module vk_gltf_viewer:gltf.algorithm.bounding_box;

import std;
export import fastgltf;

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
    [[nodiscard]] std::array<fastgltf::math::dvec3, 8> getBoundingBoxCornerPoints(const fastgltf::Asset &asset, const fastgltf::Primitive &primitive) {
        const fastgltf::Accessor &accessor = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex];

        // TODO: current glTF specification guarantees that there are min/max attributes for POSITION with
        //  dvec3 type, but KHR_mesh_quantization extension offers non-double precision POSITION attributes,
        //  which would be problematic in future. Need caution.
        const double *const pMin = std::get_if<std::pmr::vector<double>>(&accessor.min)->data();
        const double *const pMax = std::get_if<std::pmr::vector<double>>(&accessor.max)->data();

        return {
            fastgltf::math::dvec3 { pMin[0], pMin[1], pMin[2] },
            fastgltf::math::dvec3 { pMin[0], pMin[1], pMax[2] },
            fastgltf::math::dvec3 { pMin[0], pMax[1], pMin[2] },
            fastgltf::math::dvec3 { pMin[0], pMax[1], pMax[2] },
            fastgltf::math::dvec3 { pMax[0], pMin[1], pMin[2] },
            fastgltf::math::dvec3 { pMax[0], pMin[1], pMax[2] },
            fastgltf::math::dvec3 { pMax[0], pMax[1], pMin[2] },
            fastgltf::math::dvec3 { pMax[0], pMax[1], pMax[2] },
        };
    }
}