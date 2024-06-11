module;

#include <ranges>

#include <fastgltf/core.hpp>

module vk_gltf_viewer;
import :gltf.SceneResources;

vk_gltf_viewer::gltf::SceneResources::SceneResources(
    const fastgltf::Asset &asset,
    const fastgltf::Scene &scene,
    const vulkan::Gpu &gpu
) : nodeTransformBuffer { createNodeTransformBuffer(asset, scene, gpu) } { }

auto vk_gltf_viewer::gltf::SceneResources::createNodeTransformBuffer(
    const fastgltf::Asset &asset,
    const fastgltf::Scene &scene,
    const vulkan::Gpu &gpu
) const -> decltype(nodeTransformBuffer) {
    std::vector<glm::mat4> nodeTransforms(asset.nodes.size());
    const auto calculateNodeTransformsRecursive
        = [&](this const auto &self, std::size_t nodeIndex, glm::mat4 transform) -> void {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            transform *= visit(fastgltf::visitor {
                [](const fastgltf::TRS &trs) {
                    return glm::gtc::translate(glm::mat4 { 1.f }, glm::gtc::make_vec3(trs.translation.data()))
                        * glm::gtc::mat4_cast(glm::gtc::make_quat(trs.rotation.data()))
                        * glm::gtc::scale(glm::mat4 { 1.f }, glm::gtc::make_vec3(trs.scale.data()));
                },
                [](const fastgltf::Node::TransformMatrix &mat) {
                    return glm::gtc::make_mat4(mat.data());
                },
            }, node.transform);
            nodeTransforms[nodeIndex] = transform;
            for (std::size_t childIndex : node.children) {
                self(childIndex, transform);
            }
        };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        calculateNodeTransformsRecursive(nodeIndex, { 1.f });
    }

    return {
        gpu.allocator,
        std::from_range, nodeTransforms,
        vk::BufferUsageFlagBits::eStorageBuffer,
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAuto,
        },
    };
}