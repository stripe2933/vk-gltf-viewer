module;

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:gltf.SceneResources;

export import glm;
export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    export class SceneResources {
    public:
        struct NodeTransform {
            glm::mat4 matrix;
            glm::mat4 inverseMatrix = inverse(matrix);
        };

        vku::MappedBuffer nodeTransformBuffer;

        SceneResources(const fastgltf::Asset &asset, const fastgltf::Scene &scene, const vulkan::Gpu &gpu);

    private:
        [[nodiscard]] auto createNodeTransformBuffer(const fastgltf::Asset &asset, const fastgltf::Scene &scene, const vulkan::Gpu &gpu) const -> decltype(nodeTransformBuffer);
    };
}

// module :private;

static_assert(sizeof(vk_gltf_viewer::gltf::SceneResources::NodeTransform) % 64 == 0 && "minStorageBufferOffsetAlignment = 64");