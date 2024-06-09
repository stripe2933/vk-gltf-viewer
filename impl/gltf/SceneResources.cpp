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
    std::vector<NodeTransform> nodeTransforms(asset.nodes.size());
    const auto calculateNodeTransformsRecursive
        = [&](this const auto &self, std::size_t nodeIndex, fastgltf::math::fmat4x4 transform) -> void {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            transform = transform * visit(fastgltf::visitor {
                [](const fastgltf::TRS &trs) {
                    return translate(fastgltf::math::fmat4x4 { 1.f }, trs.translation)
                        * fastgltf::math::fmat4x4 { asMatrix(trs.rotation) }
                        * scale(fastgltf::math::fmat4x4 { 1.f }, trs.scale);
                },
                [](const fastgltf::math::fmat4x4 &mat) {
                    return mat;
                },
            }, node.transform);
            nodeTransforms[nodeIndex] = { .matrix = glm::gtc::make_mat4(transform.data()) };
            for (std::size_t childIndex : node.children) {
                self(childIndex, transform);
            }
        };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        calculateNodeTransformsRecursive(nodeIndex, fastgltf::math::fmat4x4 { 1.f });
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