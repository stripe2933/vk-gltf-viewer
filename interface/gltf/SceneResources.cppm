module;

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:gltf.SceneResources;

export import glm;

namespace vk_gltf_viewer::gltf {
    export class SceneResources {
    public:
        std::vector<glm::mat4> nodeWorldTransforms;

        SceneResources(const fastgltf::Asset &asset, const fastgltf::Scene &scene);
    };
}