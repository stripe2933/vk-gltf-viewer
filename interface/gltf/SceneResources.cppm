module;

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:gltf.SceneResources;

export import glm;
export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    export class SceneResources {
    public:
        vku::MappedBuffer nodeTransformBuffer;

        SceneResources(const fastgltf::Asset &asset, const fastgltf::Scene &scene, const vulkan::Gpu &gpu);

    private:
        [[nodiscard]] auto createNodeTransformBuffer(const fastgltf::Asset &asset, const fastgltf::Scene &scene, const vulkan::Gpu &gpu) const -> decltype(nodeTransformBuffer);
    };
}